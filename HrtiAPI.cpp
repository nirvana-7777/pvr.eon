/*
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "HrtiAPI.h"
#include "Curl.h"

HrtiAPI::HrtiAPI() = default;

HrtiAPI::~HrtiAPI() = default;


std::string HrtiAPI::HttpRequest()
{
  Curl curl;

  curl.AddHeader("Authorization", "Client " + MY_TOKEN);
  curl.AddHeader("User-Agent", HRTI_USER_AGENT);
  curl.AddHeader("Devicetypeid", "6");
  curl.AddHeader("Deviceid", MY_DEVICE);
  curl.AddHeader("Ipaddress", MY_IP);
  curl.AddHeader("Origin", HRTI_DOMAIN);
  curl.AddHeader("Operatorreferenceid", "hrt");
  curl.AddHeader("Content-Type", "application/json")
}


headers = {
    'connection': 'keep-alive',
    'deviceid': self.DEVICE_ID,
    'operatorreferenceid': self.__operator_reference_id,
    'authorization': 'Client ' + self.TOKEN,
    'ipaddress': str(self.IP),
    'content-type': 'application/json',
    'accept': 'application/json, text/plain, */*',
    'user-agent': self.__user_agent,
    'devicetypeid': self.__device_reference_id,
    'origin': self.hrtiDomain,
    'referer': referer,
    'accept-encoding': 'gzip, deflate, br',
    'accept-language': 'de-DE,de;q=0.9,en-US;q=0.8,en;q=0.7',
    'Cookie': cookie_header
}

def get_channels(self):

    url = self.hrtiBaseUrl + "GetChannels"

    payload = json.dumps({})
    referer = self.hrtiDomain + "/signin"
    result = self.api_post(url, payload, referer)
    if result is None:
        device_id = self.plugin.uniq_id()
        self.plugin.set_setting("device_id", device_id)
        self.register_device()
    return result

/*
    <category label="30040">
        <setting label="30041" type="number" id="devicereferenceid" default="6" enable="false"/>
        <setting label="30042" type="text" id="connectiontype" default="LAN/WiFi" enable="false"/>
        <setting label="30043" type="text" id="applicationversion" default="5.62.5" enable="false"/>
        <setting label="30044" type="text" id="osversion" default="Linux" enable="false"/>
        <setting label="30045" type="text" id="clienttype" default="Chrome 96" enable="false"/>
    </category>
    <category label="30050">
        <setting label="30051" type="text" id="operatorreferenceid" default="hrt" enable="false"/>
        <setting label="30052" type="text" id="merchant" default="aviion2" enable="false"/>
        <setting label="30053" type="text" id="apiurl" default="https://hsapi.aviion.tv/client.svc/json" enable="false"/>
        <setting label="30054" type="text" id="webapiurl" default="api/api/ott" enable="false"/>
        <setting label="30055" type="text" id="selfcareurl" default="https://hrti-selfcare.hrt.hr" enable="false"/>
    </category>
*/
