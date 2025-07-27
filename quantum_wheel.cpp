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
    return strncmp(cmd, resp, 2) == 0;
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
    sprintf(targetpos, "G%d\r\n ", position);

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
    dump(dmp, targetpos);
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
        dump(dmp, curpos);
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

void QFW::dump(char *buf, const char *data)
{
    int i = 0;
    int n = 0;
    while(data[i] != 0)
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

    dump(dmp, cmd);
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
    dump(dmp, resp);
    LOGF_DEBUG("RES <%s>", dmp);
    return nbytes;
}
