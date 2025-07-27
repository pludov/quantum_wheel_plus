/*******************************************************************************
  Copyright(c) 2016 Radek Kaczorek  <rkaczorek AT gmail DOT com>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Library General Public
 License version 2 as published by the Free Software Foundation.
 .
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Library General Public License for more details.
 .
 You should have received a copy of the GNU Library General Public License
 along with this library; see the file COPYING.LIB.  If not, write to
 the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 Boston, MA 02110-1301, USA.
*******************************************************************************/

#include "quantum_wheel.h"

#include "connectionplugins/connectionserial.h"

#include <memory>
#include <cstring>
#include <chrono>
#include <termios.h>
#include <unistd.h>

#include "indicom.h"

#define VERSION_MAJOR 0
#define VERSION_MINOR 4

#define QUANTUM_TIMEOUT 5

#define PRECISION_WORST 100.0f

std::unique_ptr<QFW> qfw(new QFW());

QFW::QFW()
{
    setDeviceName(QFW::getDefaultName());
    setVersion(VERSION_MAJOR, VERSION_MINOR);
    setFilterConnection(CONNECTION_SERIAL | CONNECTION_TCP);
}

void QFW::debugTriggered(bool enable)
{
    INDI_UNUSED(enable);
}

void QFW::simulationTriggered(bool enable)
{
    INDI_UNUSED(enable);
}

const char *QFW::getDefaultName()
{
    return (const char *)"Quantum Wheel";
}

bool QFW::readSetting(char id, float *value)
{
    char cmd[10];
    char resp[255] = {0};
    snprintf(cmd, sizeof(cmd), "s%c\r\n", id);
    if (send_command(PortFD, cmd, resp) < 2)
        return false;

    if (strncmp(cmd, resp, 2) != 0) {
        LOGF_ERROR("Unexpected reply for %c: %s", id, resp);
        return false;
    }

    char *err = nullptr;
    float f = strtod(resp + 2, &err);
    if ((!err) || *err == 0 || *err == '\r' || *err == '\n') {
        *value = f;
        return true;
    }
    LOGF_ERROR("Failed to parse float for %c from %s", id, resp + 2);
    return false;
}

bool QFW::readSettingDescription(char id, char *description, size_t size)
{
    char cmd[10];
    char resp[255] = {0};
    snprintf(cmd, sizeof(cmd), "s%c?\r\n", id);
    if (send_command(PortFD, cmd, resp) < 3)
        return false;

    if (strncmp(cmd, resp, 3) != 0) {
        LOGF_ERROR("Unexpected reply for %c: %s", id, resp);
        return false;
    }

    const char *desc = resp + 3; // Skip "s<id>?"
    strncpy(description, desc, size - 1);
    description[size - 1] = '\0'; // Ensure null termination
    return true;
}

bool QFW::Handshake()
{
    if (isSimulation())
    {
        IDMessage(getDeviceName(), "Simulation: connected");
        PortFD = 1;
        return true;
    }
    // check serial connection
    if (PortFD < 0 || isatty(PortFD) == 0)
    {
        IDMessage(getDeviceName(), "Device /dev/ttyACM0 is not available\n");
        return false;
    }
    // read description
    char cmd[10] = {"SN\r\n"};
    char resp[255] = {0};
    if (send_command(PortFD, cmd, resp) < 2)
        return false;

    // SN should respond SN<number>, this identifies a Quantum wheel
    if (strncmp(cmd, resp, 2) != 0)
        return false;

    int prevSettingCount = 0;
    this->settingCount = 0;

    // Search for a "+" sign to indicate a Custom wheel
    if (strchr(resp, '+') != nullptr)
    {
        LOG_INFO("Customized wheel detected");

        if (send_command(PortFD, "s?\r\n", resp) >= 2) {
            for(const char *p = resp+2; *p && *p != '\r' && *p != '\n' && this->settingCount < QFW_MAX_SETTINGS; p++) {
                char setting = *p;
                char setting_id[32];
                char setting_name[32];

                snprintf(setting_id, sizeof(setting_id), "SETTING_%c", setting);

                // Get the setting value
                float v;
                char description[64];
                if (this->readSetting(setting, &v) && this->readSettingDescription(setting, description, sizeof(description))) {
                    LOGF_DEBUG("Setting %c: %s = %f", setting, description, v);

                    IUFillNumber(&SettingsN[this->settingCount],
                                setting_id,
                                description,
                                "%3.5f", 0.0, 100.0, 1.0, v);
                    settingValues[this->settingCount] = v;
                    this->settingCount++;
                }
            }

            IUFillNumberVector(&SettingsNP,
                            SettingsN,
                            this->settingCount,
                            m_defaultDevice->getDeviceName(),
                            "SETTINGS",
                            "Settings",
                            "Settings",
                            IP_RW, 60,
                            IPS_IDLE);
        }
    }

    // Declare the new settings
    if (this->settingCount == 0) {
        this->m_defaultDevice->deleteProperty(SettingsNP.name);
    } else {
        this->m_defaultDevice->defineProperty(&SettingsNP);
    }

    return true;
}

bool QFW::initProperties()
{
    INDI::FilterWheel::initProperties();
    addDebugControl();
    addSimulationControl();

    serialConnection->setDefaultPort("/dev/ttyACM0");

    FilterSlotN[0].min = 1;
    FilterSlotN[0].max = 7;
    CurrentFilter      = 1;


    IUFillNumber(&FilterSwitchDurationN[0], "FILTER_SWITCH_DURATION", "Switch duration", "%3.0f", 0.0, 60.0, 1.0, 0.0);
    IUFillNumberVector(&FilterSwitchDurationNP, FilterSwitchDurationN, 1, m_defaultDevice->getDeviceName(), "DURATION", "Switch duration", "STATISTICS",
                       IP_RO, 60,
                       IPS_IDLE);
    m_defaultDevice->defineProperty(&FilterSwitchDurationNP);

    //  A number vector for precision statistics
    IUFillNumber(&FilterSwitchPrecisionN[0], "FILTER_SWITCH_PRECISION", "Switch precision", "%3.2f", 0.0, PRECISION_WORST, 1.0, PRECISION_WORST);
    IUFillNumberVector(&FilterSwitchPrecisionNP, FilterSwitchPrecisionN, 1, m_defaultDevice->getDeviceName(), "PRECISION", "Switch precision", "STATISTICS",
                        IP_RO, 60,
                        IPS_IDLE);
    m_defaultDevice->defineProperty(&FilterSwitchPrecisionNP);
    
    return true;
}

//bool QFW::updateProperties()
//{
//    INDI::FilterWheel::updateProperties();

//    if (isConnected())
//    {
//        // read number of filters
//        char cmd[10] = {"EN\r\n"};
//        char resp[255]={0};
//        if (send_command(PortFD, cmd, resp) < 2)
//            return false;
//        int numFilters = resp[1] - '0';
//        //FilterNameTP->ntp = numFilters;
//        for (int i = 0; i < numFilters; i++)
//        {
//            sprintf(cmd, "F%1d\r\n", i);
//            if (send_command(PortFD, cmd, resp) < 3)
//                return false;
//            char name[64];
//            int n = strlen(resp);
//            strncpy(name, &resp[2], n - 4);
//            name[n] = 0;
//            IUFillText(&FilterNameT[i], name, name, name);
//        }
//    }

//    return true;
//}

void QFW::ISGetProperties(const char *dev)
{
    INDI::FilterWheel::ISGetProperties(dev);
}

int QFW::QueryFilter()
{
    return CurrentFilter;
}

void QFW::UpdateFilterStatistics(float precision, int64_t duration) {
    if (precision != FilterSwitchPrecisionN[0].value)
    {
        FilterSwitchPrecisionN[0].value = precision;
        IDSetNumber(&FilterSwitchPrecisionNP, nullptr);
    }
    if (duration != FilterSwitchDurationN[0].value)
    {
        FilterSwitchDurationN[0].value = duration;
        IDSetNumber(&FilterSwitchDurationNP, nullptr);
    }
}

bool QFW::SelectFilter(int position)
{
    // count from 0 to 6 for positions 1 to 7
    position = position - 1;

    if (position < 0 || position > 6)
        return false;

    if (isSimulation())
    {
        CurrentFilter = position + 1;
        SelectFilterDone(CurrentFilter);
        UpdateFilterStatistics(PRECISION_WORST, 100);
        return true;
    }

    // goto
    char targetpos[255] = {0};
    char curpos[255] = {0};
    char dmp[255];
    int err;
    int nbytes;

    // format target position G[0-6]
    sprintf(targetpos, "\r\nG%d\r\n ", position);

    // write command
    //int len = strlen(targetpos);

    err = tty_write_string(PortFD, targetpos, &nbytes);
    if (err)
    {
        char errmsg[255];
        tty_error_msg(err, errmsg, MAXRBUF);
        LOGF_ERROR("Serial write error: %s", errmsg);
        return false;
    }
    auto start = std::chrono::system_clock::now();
    //res = write(PortFD, targetpos, len);
    dump(dmp, targetpos, nbytes);
    LOGF_DEBUG("CMD: %s", dmp);

    // format target marker P[0-6]
    sprintf(targetpos, "P%d", position);

    // check current position
    do
    {
        usleep(100 * 1000);
        //res         = read(PortFD, curpos, 255);
        err = tty_read_section(PortFD, curpos, '\n', QUANTUM_TIMEOUT, &nbytes);
        if (err)
        {
            char errmsg[255];
            tty_error_msg(err, errmsg, MAXRBUF);
            LOGF_ERROR("Serial read error: %s", errmsg);
            return false;
        }
        curpos[nbytes] = 0;
        dump(dmp, curpos, nbytes);
        LOGF_DEBUG("REP: %s", dmp);
    }
    while (strncmp(targetpos, curpos, 2) != 0);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count();

    // Check for extended status report...
    if (curpos[2] == ':')
    {
        if (curpos[3] == 'E') {
            LOGF_ERROR("Error reported by device: %s", curpos+4);
            return false;
        }
        // Reporting of precision
        LOGF_DEBUG("Precision reported by device: %s", curpos+4);
        // Parse as float
        float precision = atof(curpos+4);
        UpdateFilterStatistics(precision, duration);

    } else {
        UpdateFilterStatistics(PRECISION_WORST, duration);
    }

    // return current position to indi
    CurrentFilter = position + 1;
    SelectFilterDone(CurrentFilter);
    LOGF_DEBUG("CurrentFilter set to %d", CurrentFilter);

    return true;
}

int QFW::getSettingId(const char *name) const
{
    for(int j = 0; j < this->settingCount; j++) {
        if (strcmp(SettingsN[j].name, name) == 0) {
            return j;
        }
    }
    return -1;
}

bool QFW::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n) {
    if (FilterWheel::ISNewNumber(dev, name, values, names, n))
        return true;

    if (this->settingCount && dev && !strcmp(dev, m_defaultDevice->getDeviceName()) && !strcmp(name, SettingsNP.name)) {
        for (int i = 0; i < n; i++) {
            int settingId = getSettingId(names[i]);
            if (settingId == -1) {
                LOGF_WARN("Unknown setting: %s", names[i]);
                return false;
            }
        }

        int todo[32];
        int todoCount = 0;

        for(int i = 0; i < n; i++) {
            int settingId = getSettingId(names[i]);
            if (settingValues[settingId] != values[i]) {
                todo[todoCount++] = i;
            } else {
                LOGF_DEBUG("Setting %s already set to %f", names[i], values[i]);
            }
        }
        LOGF_DEBUG("Settings changed: %d/%d", todoCount, n);
        // FIXME: sort todo ?
        bool ok = true;
        if (todoCount > 0) {
            SettingsNP.s = IPS_BUSY;
            IDSetNumber(&SettingsNP, nullptr);


            for(int i = 0; i < todoCount; i++) {
                int idx = todo[i];
                int settingId = getSettingId(names[idx]);
                float value = values[idx];
                LOGF_INFO("Setting %s from %f to %f", names[todo[i]], settingValues[settingId],value);
                char cmd[32];
                snprintf(cmd, sizeof(cmd), "\r\ns%c%f\r\n", SettingsN[settingId].name[8], value);
                char resp[255] = {0};
                if (send_command(PortFD, cmd, resp) < 2) {
                    LOGF_ERROR("Failed to set setting %s to %f", SettingsN[settingId].name, values[idx]);
                    // Restore previous value
                    SettingsN[settingId].value = settingValues[settingId];
                    ok = false;
                } else {
                    settingValues[settingId] = value;
                }
            }

        }
        SettingsNP.s = ok ? IPS_OK : IPS_ALERT;
        IDSetNumber(&SettingsNP, nullptr);
        return true;
    }

    return false;
}


void QFW::dump(char *buf, const char *data, int data_len)
{
    int i = 0;
    int n = 0;
    while(i < data_len)
    {
        if (isprint(data[i]))
        {
            buf[n] = data[i];
            n++;
        }
        else
        {
            sprintf(buf + n, "[%02X]", data[i]);
            n += 4;
        }
        i++;
    }
    buf[n] = 0;
}

// Send a command to the mount. Return the number of bytes received or 0 if
// case of error
// commands are null terminated, replies end with /n
int QFW::send_command(int fd, const char* cmd, char *resp)
{
    int err;
    int nbytes = 0;
    char errmsg[MAXRBUF];
    int cmd_len = strlen(cmd);
    char dmp[255];

    dump(dmp, cmd, cmd_len);
    LOGF_DEBUG("CMD <%s>", dmp);

    tcflush(fd, TCIOFLUSH);
    if ((err = tty_write(fd, cmd, cmd_len, &nbytes)) != TTY_OK)
    {
        tty_error_msg(err, errmsg, MAXRBUF);
        LOGF_ERROR("Serial write error: %s", errmsg);
        return 0;
    }

    err = tty_read_section(fd, resp, '\n', QUANTUM_TIMEOUT, &nbytes);
    if (err)
    {
        tty_error_msg(err, errmsg, MAXRBUF);
        LOGF_ERROR("Serial read error: %s", errmsg);
        return 0;
    }

    resp[nbytes] = 0;
    dump(dmp, resp, nbytes);
    LOGF_DEBUG("RES <%s>", dmp);
    return nbytes;
}
