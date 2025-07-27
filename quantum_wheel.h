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

/*
 * Quantum protocol
 *
 *
 * Get number of filter positions   EN           N<number>
 * Disconnect                       DS
 * Go to filter position            G<number>
 * position is returned when the move finishes  P<number> or P<number>:E<error> or P<number>:<precision>
 * get filter name                  F<number>   F<number><name>
 * set filter name                  f<number><name>
 * Get filter offset                O<number>   O<number><offset>
 * set filter offset                o<number><offset>
 * Get description                  SN          SN<details>
 * get version                      VR          VR<details>
 *
 * Additional commands :
 * list settings                    s?          s?<id1><id2>...
 * set setting                      s<id>
 *                                  s<id><val>  s<id><val>
 * describe setting                 s<id>?      s<id>?<description>
 */

#pragma once

#include "indifilterwheel.h"

#define QFW_MAX_SETTINGS 5

class QFW : public INDI::FilterWheel
{
    public:
        QFW();
        virtual ~QFW() = default;

        void debugTriggered(bool enable);
        void simulationTriggered(bool enable);

        bool Handshake();
        const char *getDefaultName();

        bool initProperties();
        //    virtual bool updateProperties() override;

        void ISGetProperties(const char *dev);

        int QueryFilter();
        bool SelectFilter(int);
    private:
        int settingCount = 0;
        //  A number vector for statistics
        INumberVectorProperty FilterSwitchDurationNP;
        INumber FilterSwitchDurationN[1];

        //  A number vector for precision statistics
        INumberVectorProperty FilterSwitchPrecisionNP;
        INumber FilterSwitchPrecisionN[1];

        //  A number vector for precision statistics
        INumberVectorProperty SettingsNP;
        INumber SettingsN[QFW_MAX_SETTINGS];
        float settingValues[QFW_MAX_SETTINGS];

        // C'est pas virtuel, on surcharge comment ?
        virtual bool ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n) override;


        void dump(char *buf, const char *data, int data_len);
        int send_command(int fd, const char *cmd, char *resp);
        void UpdateFilterStatistics(float precision, int64_t duration);

        int getSettingId(const char *name) const;
        bool readSetting(char id, float *value);
        bool readSettingDescription(char id, char *description, size_t size);
};
