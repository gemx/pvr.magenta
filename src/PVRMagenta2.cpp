/*
 *  Copyright (C) 2011-2021 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2011 Pulse-Eight (http://www.pulse-eight.com/)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

//#include "PVRMagenta2.h"

#include <algorithm>
#include "PVRMagenta2.h"
#include "Globals.h"
#include <kodi/General.h>
#include <kodi/gui/dialogs/OK.h>
#include "Utils.h"
#include "Base64.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <tinyxml2.h>
#include <kodi/Filesystem.h>
#include "auth/AuthClient.h"
#include <unistd.h>

#define TIMER_ONCE_EPG (PVR_TIMER_TYPE_NONE + 1)
#define TIMER_SERIES_EPG (PVR_TIMER_TYPE_NONE + 2)

static const int MAGENTA_RECORDING_DELETE_MODE_WHEN_FULL = 0;
static const int MAGENTA_RECORDING_DELETE_MODE_MANUAL = 1;

void tokenize2(std::string const &str, const char* delim,
            std::vector<std::string> &out)
{
    char *token = strtok(const_cast<char*>(str.c_str()), delim);
    while (token != nullptr)
    {
        out.push_back(std::string(token));
        token = strtok(nullptr, delim);
    }
}

void replace(std::string& str, const std::string& from, const std::string& to) {
  size_t start_pos = str.find(from);
  while (start_pos != std::string::npos)
  {
    str.replace(start_pos, from.length(), to);
    start_pos = str.find(from);
  }
}

void PrepareTime(std::string& timestr)
{
  if (timestr.size() != 15)
    return;
  timestr.insert(4, "-");
  timestr.insert(7, "-");
  timestr.insert(13, ":");
  timestr.insert(16, ":");
  timestr += "Z";
}

/*
bool CPVRMagenta2::IsChannelNumberExist(const unsigned int number)
{
  for (auto& thisChannel : m_channels)
  {
    if (thisChannel.iChannelNumber == number)
    {
      return true;
    }
  }
  return false;
}
*/
bool CPVRMagenta2::XMLGetString(const tinyxml2::XMLNode* pRootNode,
                            const std::string& strTag,
                            std::string& strStringValue)
{
  const tinyxml2::XMLElement* pElement = pRootNode->FirstChildElement(strTag.c_str());
  if (!pElement)
    return false;
  const tinyxml2::XMLNode* pNode = pElement->FirstChild();
  if (pNode)
  {
    strStringValue = pNode->Value();
    return true;
  }
  strStringValue.clear();
  return false;
}

bool CPVRMagenta2::GetMyGenres()
{
  std::string content;
  kodi::vfs::CFile myFile;
  std::string file = kodi::addon::GetAddonPath() + "mygenres2.json";
  kodi::Log(ADDON_LOG_DEBUG, "Opening mygenres: %s", file.c_str());
  if (myFile.OpenFile(file))
  {
    char buffer[1024];
    while (int bytesRead = myFile.Read(buffer, 1024))
      content.append(buffer, bytesRead);
  } else {
    kodi::Log(ADDON_LOG_ERROR, "Failed to open mygenres.json");
    return false;
  }

  rapidjson::Document doc;
  doc.Parse(content.c_str());
  if (doc.GetParseError() || !doc.HasMember("genres"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to GetMyGenres");
    return false;
  }
  const rapidjson::Value& genres = doc["genres"];

  for (rapidjson::SizeType i = 0; i < genres.Size(); i++)
  {
    Magenta2Genre genre;
    genre.primaryGenre = Utils::JsonStringOrEmpty(genres[i], "primaryGenre");
    genre.genreType = Utils::JsonIntOrZero(genres[i], "genreType");
    if (genres[i].HasMember("genreSubType"))
      genre.genreSubType = Utils::JsonIntOrZero(genres[i], "genreSubType");
    else
      genre.genreSubType = -1;
    if (genres[i].HasMember("secondaryGenres"))
    {
      const rapidjson::Value& secondaryGenres = genres[i]["secondaryGenres"];
      for (rapidjson::SizeType j = 0; j < secondaryGenres.Size(); j++)
      {
        Magenta2SubGenre subGenre;
        subGenre.secondaryGenre = Utils::JsonStringOrEmpty(secondaryGenres[j], "secondaryGenre");
        subGenre.secondaryGenreType = Utils::JsonIntOrZero(secondaryGenres[j], "genreSubType");
        genre.secondaryGenres.emplace_back(subGenre);
      }
    }
    m_genres.emplace_back(genre);
    kodi::Log(ADDON_LOG_DEBUG, "Added genre: %s %i %i", genre.primaryGenre.c_str(), genre.genreType, genre.genreSubType);
  }

  return true;
}

bool CPVRMagenta2::SendDeleteRequest(const std::string& url)
{
  int statusCode = 0;
  std::string result;

  result = m_httpClient->HttpDelete(url, statusCode);

  if ((statusCode >=200 || statusCode <300))
  {
    return true;
  }
  else if (statusCode==401)
  {
      kodi::Log(ADDON_LOG_DEBUG, "We need to reauthenticate!");
      if (m_authClient->ReLogin()) 
      {
        kodi::Log(ADDON_LOG_DEBUG, "Reauth successful");
        result = m_httpClient->HttpDelete(url, statusCode);
        if ((statusCode >=200 || statusCode <300))
        {
          return true;
        }
      }
      else
      {
        kodi::Log(ADDON_LOG_DEBUG, "Reauth failed");
        kodi::gui::dialogs::OK::ShowAndGetInput("Reauth failed", "Reauth failed");
        return false;
      }
  }

  kodi::Log(ADDON_LOG_DEBUG, "Delete request for %s returned status code: %i",
                                      url.c_str(),
                                      statusCode);
  return false;
}


bool CPVRMagenta2::GetPostJson(const std::string& url, const std::string& body, rapidjson::Document& doc)
{
  int statusCode = 0;
  std::string result;

  if (body.empty()) {
    if (url.find(BOOTSTRAP_URL) != std::string::npos)// ||
      result = m_httpClient->HttpGetCached(url, 60 * 60 * 24 * 10, statusCode);
    else if ((url.find(m_allChannelStationsFeed) != std::string::npos) ||
             (url.find(m_liveTvCategoryFeed) != std::string::npos))
//             (url.find(m_entitledChannelsFeed) != std::string::npos))
      result = m_httpClient->HttpGetCached(url, 60 * 60 * 24 * 3, statusCode);
    //else if (url.find(m_pvrBaseUrl) != std::string::npos)
    //  result = m_httpClient->HttpGetCached(url, 60, statusCode);
    else
      result = m_httpClient->HttpGet(url, statusCode);
      //kodi::Log(ADDON_LOG_DEBUG, "GEMX: statuscode of get was %i and body %s",statusCode, result.c_str());
  } 
  else
  {
    result = m_httpClient->HttpPost(url, body, statusCode);
  }

  if (result.empty() && statusCode>=200 && statusCode<300)
  {
    return true;
  }
  doc.Parse(result.c_str());
  if ((doc.GetParseError()) || (statusCode != 200 && statusCode != 206))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get JSON %s status code: %i", url.c_str(), statusCode);
    return false;
  }
  if (!doc.IsArray() && doc.HasMember("isException"))
  {
    
    if (Utils::JsonIntOrZero(doc, "responseCode") == 401)
    {
      kodi::Log(ADDON_LOG_DEBUG, "We need to reauthenticate!");
      /*
      if (!m_authMethods.password && !m_authMethods.code && !m_authMethods.line)
        m_sam3Client->GetAuthMethods();
      if (m_authMethods.line)
      {
        kodi::Log(ADDON_LOG_DEBUG, "LineAuth");
//        LineAuth();
      }
      */
      if (m_authClient->ReLogin()) {
        kodi::Log(ADDON_LOG_DEBUG, "Reauth successful");
        if (body.empty()) {
          result = m_httpClient->HttpGet(url, statusCode);
        } else
        {
          //  kodi::Log(ADDON_LOG_DEBUG, "Body: %s", body.c_str());
          result = m_httpClient->HttpPost(url, body, statusCode);
          doc.Parse(result.c_str());
          if ((doc.GetParseError()) || (statusCode != 200 && statusCode != 206))
          {
            kodi::Log(ADDON_LOG_ERROR, "Failed to get JSON %s after reauth status code: %i", url.c_str(), statusCode);
            return false;
          }
        }
      } else
      {
        kodi::Log(ADDON_LOG_DEBUG, "Reauth failed");
        kodi::gui::dialogs::OK::ShowAndGetInput("Reauth failed", "Reauth failed");
        return false;
      }
      //SingleSignOn();
    }
    else
    {
      kodi::Log(ADDON_LOG_DEBUG, "Get Json for %s answered response code: %i and title %s",
                                          url.c_str(),
                                          Utils::JsonIntOrZero(doc, "responseCode"),
                                          Utils::JsonStringOrEmpty(doc, "title").c_str());
    }
    return false;
  }
  return true;
}

bool CPVRMagenta2::GetSmil(const std::string& url, tinyxml2::XMLDocument& smilDoc)
{
  int statusCode = 0;
  std::string result;

  result = m_httpClient->HttpGet(url, statusCode);

  smilDoc.Parse(result.c_str());
  if ((smilDoc.Error()) || (statusCode != 200))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get SMIL %s status code: %i", url.c_str(), statusCode);
    return false;
  }

  return true;
}

bool CPVRMagenta2::AddDistributionRight(const unsigned int number, const std::string& right)
{
  for (auto& thisChannel : m_channels)
  {
    if (thisChannel.iUniqueId == number)
    {
      thisChannel.isHidden = false;
      thisChannel.distributionRights.emplace_back(right);
      return true;
    }
  }
  kodi::Log(ADDON_LOG_DEBUG, "Failed to add distribution right: %s for channel %i", right.c_str(), number);
  return false;
}

bool CPVRMagenta2::HideDuplicateChannels()
{
  for (auto it = m_channels.begin(); it!=m_channels.end(); ++it)
  {
    for (auto it2 = it+1; it2!=m_channels.end(); ++it2)
    {
      if ((*it).iChannelNumber == (*it2).iChannelNumber)
      {
        if ((*it).isHd)
        {
          if (m_settings->PreferHigherResolution()) {
            (*it2).isHidden = true;
            kodi::Log(ADDON_LOG_DEBUG, "Hiding %s %i %i", (*it2).strChannelName.c_str(), (*it2).iChannelNumber, (*it2).iUniqueId);
          } else {
            (*it).isHidden = true;
            kodi::Log(ADDON_LOG_DEBUG, "Hiding %s %i %i", (*it).strChannelName.c_str(), (*it).iChannelNumber, (*it).iUniqueId);
          }
        } else {
          if (m_settings->PreferHigherResolution()) {
            (*it).isHidden = true;
            kodi::Log(ADDON_LOG_DEBUG, "Hiding %s %i %i", (*it).strChannelName.c_str(), (*it).iChannelNumber, (*it).iUniqueId);
          } else {
            (*it2).isHidden = true;
            kodi::Log(ADDON_LOG_DEBUG, "Hiding %s %i %i", (*it2).strChannelName.c_str(), (*it2).iChannelNumber, (*it2).iUniqueId);
          }
        }
        if ((*it).dtquality == "UHD")
        {
          (*it).isHidden = true;
          (*it2).isHidden = false;
        }
        if ((*it2).dtquality == "UHD")
        {
          (*it2).isHidden = true;
          (*it).isHidden = false;
        }
      }
    }
  }
  return true;
}

bool CPVRMagenta2::Bootstrap()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  m_deviceTokensUrl.clear();
  m_manifestBaseUrl.clear();
  m_clientModel.clear();
  m_deviceModel.clear();

  std::string url = BOOTSTRAP_URL;
  replace(url, "{configGroupId}", Magenta2Parameters[m_platform].config_group_id);
  url = url + "deviceid=" + m_deviceId;

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return false;
  }

  if (doc.HasMember("baseSettings"))
  {
    const rapidjson::Value& baseSettings = doc["baseSettings"];
    m_clientModel = Utils::JsonStringOrEmpty(baseSettings, "clientModel");
    m_deviceModel = Utils::JsonStringOrEmpty(baseSettings, "deviceModel");
    m_accountBaseUrl = Utils::JsonStringOrEmpty(baseSettings, "accountBaseUrl");
    m_deviceTokensUrl = Utils::JsonStringOrEmpty(baseSettings, "deviceTokensUrl");
    m_authClient->SetSam3Url(Utils::JsonStringOrEmpty(baseSettings, "sam3Url"));
    m_authClient->SetSam3ClientId(Utils::JsonStringOrEmpty(baseSettings, "sam3ClientId"));
    m_authClient->SetLineAuthUrl(Utils::JsonStringOrEmpty(baseSettings, "lineAuthUrl") + "?" +
                                "%24deviceModel=" + m_deviceModel + "&" +
                                "redirect=false&" +
                                "%24theme=" + Utils::JsonStringOrEmpty(baseSettings, "themeId") + "&" +
                                "%24profile=" + Utils::JsonStringOrEmpty(baseSettings, "profileName"));
    m_authClient->SetTaaUrl(Utils::JsonStringOrEmpty(baseSettings, "taaUrl"));
    if (doc.HasMember("dcm"))
    {
      const rapidjson::Value& dcm = doc["dcm"];
      m_manifestBaseUrl = Utils::JsonStringOrEmpty(dcm, "manifestBaseUrl");
    }
  } else {
    kodi::Log(ADDON_LOG_ERROR, "Bootstrap returned without expected parameters");
    return false;
  }

  return true;
}

bool CPVRMagenta2::GetParameter(const std::string& key, std::string& value)
{
  for (const auto& parameter : m_parameters)
  {
    if (parameter.key == key) {
      value = parameter.value;
      return true;
    }
  }
  return false;
}

bool CPVRMagenta2::GetCategories()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  if (m_settings->IsOnlyFavorites())
  {
      Magenta2Category category;
      category.id = "Favorites";
      category.description = "Favorite channels";
      category.parentId = "";
      category.order = 1;
      //category.scheme = Utils::JsonStringOrEmpty(entries[i], "scheme");
      category.level = 0; //Utils::JsonIntOrZero(entries[i], "level");
      m_categories.emplace_back(category);
      kodi::Log(ADDON_LOG_DEBUG, "Adding category %s", category.description.c_str());          

      return true;
  }

  replace(m_liveTvCategoryFeed, "{MpxAccountPid}", m_accountPid);

  std::string url = m_liveTvCategoryFeed;

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return false;
  }

  if (!doc.HasMember("entries"))
    return false;

  const rapidjson::Value& entries = doc["entries"];
  for (rapidjson::SizeType i = 0; i < entries.Size(); i++)
  {
    Magenta2Category category;
    category.id = Utils::JsonStringOrEmpty(entries[i], "id");
    category.description = Utils::JsonStringOrEmpty(entries[i], "description");
    category.parentId = Utils::JsonStringOrEmpty(entries[i], "parentId");
    category.order = Utils::JsonIntOrZero(entries[i], "order");
    category.scheme = Utils::JsonStringOrEmpty(entries[i], "scheme");
    category.level = Utils::JsonIntOrZero(entries[i], "level");
    m_categories.emplace_back(category);
    kodi::Log(ADDON_LOG_DEBUG, "Adding category %s", category.description.c_str());
  }

  return true;
}

bool CPVRMagenta2::DeviceManifest()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string url = m_deviceTokensUrl +
                    "?model=" + Utils::UrlEncode(Magenta2Parameters[m_platform].app_model) +
                    "&deviceId=" + m_deviceId +
                    "&appname=" + Magenta2Parameters[m_platform].app_name +
                    "&appVersion=" + Magenta2Parameters[m_platform].app_version +
                    "&firmware=" + Utils::UrlEncode(Magenta2Parameters[m_platform].firmware) +
                    "&runtimeVersion=" + Magenta2Parameters[m_platform].runtime +
                    "&duid=" + m_deviceId;

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return false;
  }

  if (!doc.HasMember("settings") || !doc.HasMember("sts"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to load device manifest");
    return false;
  }
  const rapidjson::Value& settings = doc["settings"];
  if (settings.HasMember("parameters")) {
    m_parameters.clear();
    const rapidjson::Value& parameters = settings["parameters"];
    for (rapidjson::SizeType i = 0; i < parameters.Size(); i++)
    {
      Magenta2KV kvpair;
      kvpair.key = Utils::JsonStringOrEmpty(parameters[i], "key");
      kvpair.value = Utils::JsonStringOrEmpty(parameters[i], "value");
      m_parameters.emplace_back(kvpair);
    }
    GetParameter("imageScalingBasicUrl", m_ngiss.basicUrl);
    GetParameter("imageScalingCallParameter", m_ngiss.callParameter);
    GetParameter("mpxBasicUrlGetApplicableDistributionRights", m_basicUrlGetApplicableDistributionRights);
//    GetParameter("LineAuthURL", m_lineAuthUrl);
    GetParameter("mpxLocationIdUri", m_locationIdUri);
    GetParameter("mpxAccountPid", m_accountPid);
    GetParameter("mpxBasicUrlAllChannelSchedulesFeed", m_allChannelSchedulesFeed);
    GetParameter("mpxAllProgramsFeedUrl", m_allProgramsFeedUrl);
    GetParameter("widevineLicenseAcquisitionURL", m_widevineLicenseAcquisitionUrl);
    GetParameter("mpxBasicUrlEntitledChannelsFeed", m_entitledChannelsFeed);
    GetParameter("mpxBasicUrlSelectorService", m_basicUrlSelectorService);
    GetParameter("mpxUserProfileUrl", m_userProfileUrl);
    replace(m_entitledChannelsFeed, "{{MpxAccountPid}}", m_accountPid);
    m_liveTvCategoryFeed = "https://feed.media.theplatform.eu/f/{MpxAccountPid}/{MpxAccountPid}-livetv-categories/Category";
    GetParameter("mpxPvrBaseUrl", m_pvrBaseUrl);
    GetParameter("mpxAccountUri", m_mpxAccountUri);
    m_authClient->SetAccountUri(m_mpxAccountUri);
  }
  const rapidjson::Value& sts = doc["sts"];
  m_authClient->SetAuthorizeTokenUrl(Utils::JsonStringOrEmpty(sts, "authorizeTokensUrl"));
  std::string deviceToken = Utils::JsonStringOrEmpty(sts, "deviceToken");
  m_authClient->SetDeviceToken(deviceToken);
  m_httpClient->SetDeviceToken(deviceToken);
  m_authClient->InitSam3();

  return true;
}

bool CPVRMagenta2::Manifest()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  replace(m_manifestBaseUrl, "{configGroupId}", Magenta2Parameters[m_platform].config_group_id);
  std::string url = m_manifestBaseUrl + "?deviceid=" + m_deviceId;

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return false;
  }

  if (!doc.HasMember("mpx") || !doc.HasMember("livetv") || !doc.HasMember("ngiss"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to load manifest");
    return false;
  }
  const rapidjson::Value& mpx = doc["mpx"];
  m_accountPid = Utils::JsonStringOrEmpty(mpx, "accountPid");
  m_locationIdUri = Utils::JsonStringOrEmpty(mpx, "locationIdUri");
  m_pvrBaseUrl = Utils::JsonStringOrEmpty(mpx, "pvrBaseUrl");
  m_userProfileUrl = Utils::JsonStringOrEmpty(mpx, "userProfileUrl");
  m_basicUrlGetApplicableDistributionRights = Utils::JsonStringOrEmpty(mpx, "basicUrlGetApplicableDistributionRights");
  m_basicUrlSelectorService = Utils::JsonStringOrEmpty(mpx, "basicUrlSelectorService");
  if (mpx.HasMember("feeds"))
  {
    const rapidjson::Value& feeds = mpx["feeds"];
    m_entitledChannelsFeed = Utils::JsonStringOrEmpty(feeds, "entitledChannelsFeed");
    m_allChannelSchedulesFeed = Utils::JsonStringOrEmpty(feeds, "allChannelSchedulesFeed");
    m_allChannelStationsFeed = Utils::JsonStringOrEmpty(feeds, "allChannelStationsFeed");
    m_allListingsFeedUrl = Utils::JsonStringOrEmpty(feeds, "allListingsFeedUrl");
    m_allProgramsFeedUrl = Utils::JsonStringOrEmpty(feeds, "allProgramsFeedUrl");
    m_liveTvCategoryFeed = Utils::JsonStringOrEmpty(feeds, "liveTvCategoryFeed");
  }
  const rapidjson::Value& livetv = doc["livetv"];
  if (livetv.HasMember("drm"))
  {
    const rapidjson::Value& drm = livetv["drm"];
    m_widevineLicenseAcquisitionUrl = Utils::JsonStringOrEmpty(drm, "widevineLicenseAcquisitionUrl");
  }
  const rapidjson::Value& ngiss = doc["ngiss"];
  m_ngiss.basicUrl = Utils::JsonStringOrEmpty(ngiss, "basicUrl");
  m_ngiss.callParameter = Utils::JsonStringOrEmpty(ngiss, "callParameter");

//  kodi::Log(ADDON_LOG_DEBUG, "m_entitledChannelsFeed: %s", m_entitledChannelsFeed.c_str());

  return true;
}

bool CPVRMagenta2::GetDistributionRights()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  m_distributionRights.clear();
  std::string url = m_basicUrlGetApplicableDistributionRights + "?form=json&schema=1.2";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return false;
  }

  if (!doc.HasMember("getApplicableDistributionRightsResponse"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get distribution rights");
    return false;
  }
  const rapidjson::Value& rights = doc["getApplicableDistributionRightsResponse"];
  for (rapidjson::SizeType i = 0; i < rights.Size(); i++)
  {
    std::string right = rights[i].GetString();
    m_distributionRights.emplace_back(right);
    kodi::Log(ADDON_LOG_DEBUG, "Added right: %s", right.c_str());
  }

  return true;
}

void CPVRMagenta2::AddGroupChannel(const std::string& id, const int& channelUid)
{
  for (auto& category : m_categories)
  {
    if (id == category.id)
    {
      category.channelUids.emplace_back(channelUid);
    }
  }
}

void CPVRMagenta2::AddChannelEntry(const rapidjson::Value& entry)
{
  Magenta2Channel channel;
  channel.iUniqueId = Utils::JsonIntOrZero(entry, "channelNumber");
  channel.title = Utils::JsonStringOrEmpty(entry, "title");
  channel.id = Utils::JsonStringOrEmpty(entry, "id");
  channel.iChannelNumber = Utils::JsonIntOrZero(entry, "dt$displayChannelNumber");
  if (m_settings->HideUnsubscribed())
    channel.isHidden = true;
  else
    channel.isHidden = false;
  channel.bRadio = false;
  channel.isFavorite = false;
  const rapidjson::Value& stations = entry["stations"];
  for (rapidjson::Value::ConstMemberIterator itr = stations.MemberBegin(); itr != stations.MemberEnd(); ++itr)
  {
    const rapidjson::Value& stationItem = (itr->value);
    channel.stationsId = Utils::JsonStringOrEmpty(stationItem, "id");
    channel.strChannelName = Utils::JsonStringOrEmpty(stationItem, "title");
    channel.isHd = Utils::JsonBoolOrFalse(stationItem, "isHd");
    channel.dtquality = Utils::JsonStringOrEmpty(stationItem, "dt$quality");
    const rapidjson::Value& mediaPids = stationItem["era$mediaPids"];
    channel.mediaPath = Utils::JsonStringOrEmpty(mediaPids, "urn:theplatform:tv:location:any");
    if (stationItem.HasMember("thumbnails"))
    {
      const rapidjson::Value& thumbnails = stationItem["thumbnails"];
      for (int i=0; i<Magenta2StationThumbnailTypes.size(); i++)
      {
        if (thumbnails.HasMember(Magenta2StationThumbnailTypes[i].c_str()))
        {
          const rapidjson::Value& thumbnail = thumbnails[Magenta2StationThumbnailTypes[i].c_str()];
          Magenta2Picture picture;
          picture.title = Utils::JsonStringOrEmpty(thumbnail, "title");
          picture.url = Utils::JsonStringOrEmpty(thumbnail, "url");
          picture.width = Utils::JsonIntOrZero(thumbnail, "width");
          picture.height = Utils::JsonIntOrZero(thumbnail, "height");
          channel.thumbnails.emplace_back(picture);
        }
      }
    }

    
    if (!m_settings->IsOnlyFavorites())
    {
      if (stationItem.HasMember("dt$categoryIds"))
      {
        const rapidjson::Value& categoryIds = stationItem["dt$categoryIds"];
        for (int j=0; j<categoryIds.Size(); j++)
        {
          AddGroupChannel(categoryIds[j].GetString(), channel.iUniqueId);
        }
      }
    }
    CPVRMagenta2::m_channels.emplace_back(channel);
    kodi::Log(ADDON_LOG_DEBUG, "Added channel: %u %s station: %s id: %s", channel.iUniqueId, channel.strChannelName.c_str(), channel.stationsId.c_str(), channel.id.c_str());
  }
}

void CPVRMagenta2::AddEntitlementEntry(const rapidjson::Value& entry)
{
  const rapidjson::Value& rights = entry["distributionRightIds"];
  unsigned int channelNumber = Utils::JsonIntOrZero(entry, "dt$channelNumber");
  for (rapidjson::SizeType i = 0; i < rights.Size(); i++)
  {
    AddDistributionRight(channelNumber, rights[i].GetString());
  }
}

bool CPVRMagenta2::GetFeed(/*const int& feed,*/ const int& maxEntries, /*const std::string& params,*/ std::string& baseUrl/*, kodi::addon::PVREPGTagsResultSet& results*/,
        handleentry_t HandleEntry)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  int startIndex = 1;
  int endIndex = maxEntries;
  bool nextRequest = true;

  std::string url;
  std::string range;
  rapidjson::Document doc;

  while (nextRequest) {
    range = std::to_string(startIndex) + "-";
    if (endIndex != 0)
      range += std::to_string(endIndex);
    url = baseUrl + "&range=" + range;

    if (!GetPostJson(url, "", doc)) {
      return false;
    }

    if (!doc.HasMember("entries"))
    {
      kodi::Log(ADDON_LOG_ERROR, "Failed to get feed");
      return false;
    }
    int entryCount = Utils::JsonIntOrZero(doc, "entryCount");
    if (entryCount < maxEntries)
      nextRequest = false;
    else {
      startIndex += entryCount;
      endIndex += entryCount;
    }
    const rapidjson::Value& entries = doc["entries"];
    for (rapidjson::SizeType i = 0; i < entries.Size(); i++)
    {
      (this->*HandleEntry)(entries[i]);
/*
      switch (feed) {
        case FEED_ALL_CHANNELS:
          AddChannelEntry(entries[i]);
          break;
        case FEED_ENTITLED_CHANNELS:
          AddEntitlementEntry(entries[i]);
          break;
        case FEED_CHANNEL_SCHEDULE:
          const rapidjson::Value& listings = entries[i]["listings"];
          for (rapidjson::SizeType j = 0; j < entries.Size(); j++)
          {

          }
          break;
      }
*/
    }
  }

  return true;
}

void CPVRMagenta2::SetChannelNumber(const std::string& id, const int& number)
{
  for (auto& thisChannel : m_channels)
  {
    if (thisChannel.stationsId == id)
    {
      thisChannel.iChannelNumber = number;
      if (number==0) // 0 means hidden by channel manager of magenta tv
      {
        thisChannel.isHidden=true;
      }
      kodi::Log(ADDON_LOG_DEBUG, "Setting new personal channel number %i for channel: %s", number, thisChannel.strChannelName.c_str());
    }
  }
}

void CPVRMagenta2::SetFavorite(const std::string& id, const int& number)
{
  for (auto& thisChannel : m_channels)
  {
    if (thisChannel.stationsId == id)
    {
      thisChannel.isFavorite = true;
      kodi::Log(ADDON_LOG_DEBUG, "Making %s a favorite channel", thisChannel.strChannelName.c_str());
    }
  }
}

bool CPVRMagenta2::GetUserList(const std::string& context)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  if (m_userProfileUrl.empty())
    return false;

  std::string url = m_userProfileUrl + "/data/UserList?form=cjson&schema=1.3";

  if (!context.empty())
  {
    url += "&byContext=" + context;
  }

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return false;
  }

  if (!doc.HasMember("entries"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get UserList");
    return false;
  }

  const rapidjson::Value& entries = doc["entries"];
  for (rapidjson::SizeType i = 0; i < entries.Size(); i++)
  {
    if (entries[i].HasMember("items"))
    {
      const rapidjson::Value& items = entries[i]["items"];
      std::string title = Utils::JsonStringOrEmpty(entries[i], "title");
      if (title == "LiveTvPersonalChannelList")
      {
        for (rapidjson::SizeType j = 0; j < items.Size(); j++)
        {
          SetChannelNumber(Utils::JsonStringOrEmpty(items[j], "aboutId"), Utils::JsonIntOrZero(items[j], "index"));
        }
      }
      if (title == "LiveTvFavoriteChannelList")
      {
        for (rapidjson::SizeType j = 0; j < items.Size(); j++)
        {
          std::string id=Utils::JsonStringOrEmpty(items[j], "aboutId");
          if (m_settings->IsOnlyFavorites())
          {
            for (auto& thisChannel : m_channels)
            {
              if (thisChannel.stationsId == id)
              {
                m_categories[0].channelUids.emplace_back(thisChannel.iUniqueId);
                kodi::Log(ADDON_LOG_DEBUG, "Added %s to favorite channels", thisChannel.strChannelName.c_str());
                break;
              }
            }
          }
        }
      }
    }
  }

  return true;
}

CPVRMagenta2::CPVRMagenta2(CSettings* settings, HttpClient* httpclient, kodi::addon::CInstancePVRClient* instancePVRClient):
  m_settings(settings),
  m_httpClient(httpclient),
  m_instancePVRClient(instancePVRClient)
{
  m_sessionId = Utils::CreateUUID();
  kodi::Log(ADDON_LOG_DEBUG, "Current SessionID %s", m_sessionId.c_str());
  m_httpClient->SetSessionId(m_sessionId);
  m_deviceId = m_settings->GetMagentaDeviceID();
  m_platform = m_settings->GetTerminalType();
  if (m_deviceId.empty()) {
    m_deviceId = Utils::CreateUUID();
    m_settings->SetSetting("deviceid", m_deviceId);
  }
  kodi::Log(ADDON_LOG_DEBUG, "Current DeviceID %s", m_deviceId.c_str());
  m_authClient = new AuthClient(m_settings, m_httpClient);
  m_httpClient->SetAuthClient(m_authClient);

  if (!Bootstrap())
    return;
  if (!m_deviceTokensUrl.empty()) {
    if (!DeviceManifest())
      return;
  } else if (!m_manifestBaseUrl.empty()) {
    if (!Manifest())
      return;
  } else
  {
    kodi::Log(ADDON_LOG_DEBUG, "No appropriate URL found");
    return;
  }
  m_categories.clear();
  if (m_settings->IsGroupsenabled())
    GetCategories();

  m_channels.clear();
  if (m_allChannelStationsFeed.empty())
  {
    if (!GetParameter("mpxDefaultUrlAllChannelStationsFeed", m_allChannelStationsFeed))
      return;
  } else {
    replace(m_allChannelStationsFeed, "{MpxAccountPid}", m_accountPid);
  }
  std::string baseUrl = m_allChannelStationsFeed + "?lang=short-de";

  GetFeed(/*FEED_ALL_CHANNELS,*/ MAX_CHANNEL_ENTRIES, baseUrl/*, nullptr*/, &CPVRMagenta2::AddChannelEntry);

  //TODO: Remove
//  m_sam3Client->Sam3Login();

  GetDistributionRights();

  replace(m_entitledChannelsFeed, "{MpxAccountPid}", m_accountPid);
  baseUrl = m_entitledChannelsFeed + "?byDistributionRightId=";
  for (const auto& right : m_distributionRights)
  {
    if (baseUrl != m_entitledChannelsFeed + "?byDistributionRightId=")
      baseUrl += "%7C"; // "|"
    baseUrl += Utils::UrlEncode(right);
  }
  GetFeed(/*FEED_ENTITLED_CHANNELS,*/ MAX_CHANNEL_ENTRIES, baseUrl/*, nullptr*/, &CPVRMagenta2::AddEntitlementEntry);
  HideDuplicateChannels();
  replace(m_allChannelSchedulesFeed, "{{MpxAccountPid}}", m_accountPid);
  replace(m_allProgramsFeedUrl, "{{MpxAccountPid}}", m_accountPid);
  GetMyGenres();
  GetUserList("");
  
  m_refreshTimer = std::make_unique<kodi::tools::CTimer>([this] { OnRefreshEveryMinute(); });
  // Start in 60s, wiederkehrend alle 60s
  m_refreshTimer->Start(60 * 1000, true /*repeat*/);

}

CPVRMagenta2::~CPVRMagenta2()
{
  if (m_refreshTimer) 
  {
    m_refreshTimer->Stop();
  }
  m_channels.clear();
}

PVR_ERROR CPVRMagenta2::GetCapabilities(kodi::addon::PVRCapabilities& capabilities)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  capabilities.SetSupportsEPG(true);
  capabilities.SetSupportsEPGEdl(false);
  capabilities.SetSupportsTV(true);
  capabilities.SetSupportsRadio(false);
  capabilities.SetSupportsChannelGroups(m_settings->IsGroupsenabled());
  capabilities.SetSupportsRecordings(true);
  capabilities.SetSupportsRecordingsDelete(true);
  capabilities.SetSupportsRecordingsUndelete(false);
  capabilities.SetSupportsRecordingsRename(false);
  capabilities.SetSupportsRecordingsLifetimeChange(false);
  capabilities.SetSupportsLastPlayedPosition(false);
  capabilities.SetSupportsTimers(true);
  capabilities.SetSupportsDescrambleInfo(false);
  capabilities.SetSupportsProviders(false);
  /* PVR recording lifetime values and presentation.*/
//  std::vector<kodi::addon::PVRTypeIntValue> lifetimeValues;
//  GetLifetimeValues(lifetimeValues, true);
//  capabilities.SetRecordingsLifetimeValues(lifetimeValues);
  //capabilities.SetSupportsAsyncEPGTransfer(true);
//  capabilities.SetHandlesInputStream(true);

  return PVR_ERROR_NO_ERROR;
}

bool CPVRMagenta2::GetStreamParameters(const std::string& url, std::string& src, std::string& releasePid)
{
  tinyxml2::XMLDocument smilDoc;

  if (!GetSmil(url, smilDoc))
    return false;

  tinyxml2::XMLElement* pRootElement = smilDoc.RootElement();
//  kodi::Log(ADDON_LOG_DEBUG, "Root: %s", pRootElement->Value().c_str());
  tinyxml2::XMLElement* pElement = pRootElement->FirstChildElement("body");
  if (!pElement) {
    kodi::Log(ADDON_LOG_ERROR, "No Body found");
    return false;
  }
  tinyxml2::XMLElement* pHead = pRootElement->FirstChildElement("head");
  if (!pHead) {
    kodi::Log(ADDON_LOG_ERROR, "No Head found");
    return false;
  }
  tinyxml2::XMLElement* pMeta = pHead->FirstChildElement("meta");
  std::string name;
  std::string content;
  while (pMeta)
  {
    name = pMeta->Attribute("name");
    content = pMeta->Attribute("content");
    if (name == "concurrencyInstance")
      m_currentLock.instance = content;
    else if (name == "updateLockInterval")
      m_currentLock.updateInterval = stoi(content);
    else if (name == "concurrencyServiceUrl")
      m_currentLock.serviceUrl = content;
    else if (name == "lockId")
      m_currentLock.id = content;
    else if (name == "lockSequenceToken")
      m_currentLock.token = content;
    else if (name == "lock")
      m_currentLock.lock = content;
    else
      kodi::Log(ADDON_LOG_DEBUG, "Unknown Meta Content name: %s with content: %s", name.c_str(), content.c_str());
    pMeta = pMeta->NextSiblingElement();
  }
  tinyxml2::XMLElement* seq = pElement->FirstChildElement("seq");
  if (!seq) {
    kodi::Log(ADDON_LOG_ERROR, "No seq found");
    return false;
  }
  tinyxml2::XMLElement* reffailed = seq->FirstChildElement("ref");
  tinyxml2::XMLElement* switchgood = seq->FirstChildElement("switch");
  std::string value;
  std::string trackingData;
  releasePid.clear();
  src.clear();
  trackingData.clear();
  if (reffailed) {
    src = reffailed->Attribute("src");
    kodi::Log(ADDON_LOG_DEBUG, "SRC: %s", src.c_str());
    std::string title = reffailed->Attribute("title");
    kodi::Log(ADDON_LOG_DEBUG, "Title: %s", title.c_str());
    std::string abstract = reffailed->Attribute("abstract");
    kodi::Log(ADDON_LOG_DEBUG, "Abstract: %s", abstract.c_str());
    tinyxml2::XMLElement* pParm = reffailed->FirstChildElement("param");
    bool isException = false;
    int responseCode = 0;
    while (pParm) {
      name = pParm->Attribute("name");
      value = pParm->Attribute("value");
      kodi::Log(ADDON_LOG_DEBUG, "Param Name: %s Value: %s", name.c_str(), value.c_str());
      if ((name == "isException") && (value == "true"))
        isException = true;
      if (name == "responseCode")
      {
        try {
          responseCode = std::stoi(value);
        } catch (const std::exception& e) {}
      }
      pParm = pParm->NextSiblingElement();
    }
    if (isException)
      kodi::gui::dialogs::OK::ShowAndGetInput(title, abstract);
    return false;
  } else if (switchgood) {
    tinyxml2::XMLElement* refgood = switchgood->FirstChildElement("ref");
    src = refgood->Attribute("src");
    tinyxml2::XMLElement* ppParm = refgood->FirstChildElement("param");
    if (ppParm) {
      name = ppParm->Attribute("name");
      value = ppParm->Attribute("value");
      if (name == "trackingData") {
        trackingData = value;
        kodi::Log(ADDON_LOG_DEBUG, "Tracking Data: %s", trackingData.c_str());
        std::vector<std::string> out;
        tokenize2(trackingData, "|", out);
        for (const auto& data : out)
        {
    //      kodi::Log(ADDON_LOG_DEBUG, "Tracking Data: %s", data.c_str());
          if (data.substr(0,4) == "pid=") {
            releasePid = data;
            releasePid.erase(0,4);
          }
        }
      }
    }
  } else {
    kodi::Log(ADDON_LOG_ERROR, "Unknown structure");
    return false;
  }
  return true;
}

bool CPVRMagenta2::ReleaseLock()
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  if (m_currentLock.serviceUrl.empty())
    return false;

  std::string url = m_currentLock.serviceUrl + "/web/Concurrency/unlock?_clientId=" + m_deviceId +
                                               "&_encryptedLock=" + Utils::UrlEncode(m_currentLock.lock) +
                                               "&_id=" + Utils::UrlEncode(m_currentLock.id) +
                                               "&_sequenceToken=" + Utils::UrlEncode(m_currentLock.token) +
                                               "&form=json&schema=1.0";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return false;
  }

  return true;
}

PVR_ERROR CPVRMagenta2::SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
                                    const std::string& url,
                                    bool realtime, bool playTimeshiftBuffer, bool epgplayback)
{
  properties.emplace_back(PVR_STREAM_PROPERTY_ISREALTIMESTREAM, realtime ? "true" : "false");
  properties.emplace_back(PVR_STREAM_PROPERTY_EPGPLAYBACKASLIVE, epgplayback ? "true" : "false");

  std::string src;
  std::string releasePid;
  GetStreamParameters(url + "?format=SMIL&formats=MPEG-DASH&tracking=true&clientId=player_" + m_deviceId, src, releasePid);
  if (src.empty()) {
    return PVR_ERROR_FAILED;
  }
  if (src.find("begin=") != std::string::npos)
  {
    std::string begin = src.substr(src.find("begin=") + 6, 15);
    std::string end = src.substr(src.find("end=") + 4, 15);
    PrepareTime(begin);
    PrepareTime(end);
    time_t beginTime = Utils::StringToTime2(begin);
    kodi::Log(ADDON_LOG_DEBUG, "Begin time: %s / %u end time: %s / %u", begin.c_str(), beginTime, end.c_str(), Utils::StringToTime2(end));
    if (time(NULL) - TIMEBUFFER2 > beginTime)
    {
      std::string newBegin = Utils::TimeToString3(time(NULL) - TIMEBUFFER2 + 10);
      kodi::Log(ADDON_LOG_DEBUG, "New begin time: %s", newBegin.c_str());
      src.replace(src.find("begin=") + 6, 15, newBegin);
    }
  }
  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, src);
  kodi::Log(ADDON_LOG_DEBUG, "[PLAY STREAM] url: %s", src.c_str());
  if (!releasePid.empty()) {
    std::string personaToken;
    if (!m_authClient->GetPersonaToken(personaToken))
      return PVR_ERROR_FAILED;

    personaToken = base64_decode(personaToken);
    kodi::Log(ADDON_LOG_DEBUG, "PersonaToken: %s", personaToken.c_str());
    personaToken.erase(0, m_accountBaseUrl.length());
    std::string account = m_accountBaseUrl + personaToken.substr(0, personaToken.find(":"));
    kodi::Log(ADDON_LOG_DEBUG, "Account: %s", account.c_str());
    personaToken.erase(0, personaToken.find(":") + 1);
    kodi::Log(ADDON_LOG_DEBUG, "Token: %s", personaToken.c_str());

    kodi::Log(ADDON_LOG_DEBUG, "ReleasePid: %s", releasePid.c_str());

    std::string lkey = m_widevineLicenseAcquisitionUrl + "?account=" + Utils::UrlEncode(account) +
                                                         "&releasePid=" + releasePid +
                                                         "&token=" + personaToken +
                                                         "&schema=1.0";

//    std::string lkey = m_widevineLicenseAcquisitionUrl + "?releasePid=" + releasePid +
//                                                         "&schema=1.0";

    lkey += "|"
//            "Origin=https://web2.magentatv.de"
//            "&Referer=https://web2.magentatv.de"
//            "&User-Agent=Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/117.0.0.0 Safari/537.36"
            "User-Agent=" + Magenta2Parameters[m_platform].user_agent +
//            "&Authorization=Basic " + personaToken +
            "&Content-Type= "
            "|R{SSM}|";
    kodi::Log(ADDON_LOG_DEBUG, "Licence Key: %s", lkey.c_str());
    properties.emplace_back(PVR_STREAM_PROPERTY_MIMETYPE, "application/xml+dash");
    properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
    properties.emplace_back("inputstream.adaptive.manifest_headers", "User-Agent=" + Magenta2Parameters[m_platform].user_agent);
    properties.emplace_back("inputstream.adaptive.stream_headers", "User-Agent=" + Magenta2Parameters[m_platform].user_agent);
    //properties.emplace_back("inputstream.adaptive.license_key", lkey);
    std::string urlFirst;
    std::string urlSecond;
    if (lkey.length() > CUTOFF) {
        urlFirst = lkey.substr(0,CUTOFF);
        urlSecond = lkey.substr(CUTOFF,std::string::npos);
        kodi::Log(ADDON_LOG_DEBUG, "First %s", urlFirst.c_str());
        kodi::Log(ADDON_LOG_DEBUG, "Second %s", urlSecond.c_str());
        properties.emplace_back("inputstream.adaptive.license_url", urlFirst);
        properties.emplace_back("inputstream.adaptive.license_url_append", urlSecond);
    }
//    properties.emplace_back("inputstream.adaptive.play_timeshift_buffer", playTimeshiftBuffer ? "true" : "false");
//    properties.emplace_back("inputstream.adaptive.manifest_type", "mpd");
    properties.emplace_back("inputstream.adaptive.license_type", "com.widevine.alpha");
  }

//  properties.emplace_back("inputstream.adaptive.manifest_update_parameter", "full");
  return PVR_ERROR_NO_ERROR;
}

/*******************************************************************************************************************************************************
*                                                              Channels                                                                                *
*******************************************************************************************************************************************************/
bool CPVRMagenta2::GetChannelNamebyId(const std::string& id, std::string& name)
{
//  kodi::Log(ADDON_LOG_DEBUG, "Looking for station ID: %s", id.c_str());
  for (const auto& thisChannel : m_channels)
  {
    if (thisChannel.stationsId == id)
    {
//      kodi::Log(ADDON_LOG_DEBUG, "Found name %s for id %s:", name.c_str(), id.c_str());
      name = thisChannel.strChannelName;
      return true;
    }
  }
  return false;
}

PVR_ERROR CPVRMagenta2::GetChannelsAmount(int& amount)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  amount = m_channels.size();
  std::string amount_str = std::to_string(amount);
  kodi::Log(ADDON_LOG_DEBUG, "Channels Amount: [%s]", amount_str.c_str());
  return PVR_ERROR_NO_ERROR;
}

std::string CPVRMagenta2::GetNgissUrl(const std::string& url, const int& width, const int& height)
{
  return m_ngiss.basicUrl + "iss/?" +
         m_ngiss.callParameter + "&ar=keep" +
         "&src=" + Utils::UrlEncode(url) +
         "&x=" + std::to_string(width) +
         "&y=" + std::to_string(height);
}

PVR_ERROR CPVRMagenta2::GetChannels(bool bRadio, kodi::addon::PVRChannelsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  int startnum = m_settings->GetStartNum()-1;
  for (const auto& channel : m_channels)
  {

    if ((channel.bRadio == bRadio) && (!m_settings->IsHiddenDeactivated() || !channel.isHidden)) 
    {
      kodi::addon::PVRChannel kodiChannel;

      kodiChannel.SetUniqueId(static_cast<unsigned int>(channel.iUniqueId));
      kodiChannel.SetIsRadio(channel.bRadio);
      kodiChannel.SetChannelNumber(static_cast<unsigned int>(startnum + channel.iChannelNumber));
      kodiChannel.SetChannelName(channel.strChannelName);

      int pictureNo = 0;
      for (int i=0; i<channel.thumbnails.size(); i++)
      {
        if ((m_settings->UseWhiteLogos() && channel.thumbnails[i].title == "stationLogo.png") ||
            (!m_settings->UseWhiteLogos() && channel.thumbnails[i].title == "stationLogoColored.png"))
          pictureNo = i;
      }
      std::string iconUrl = GetNgissUrl(channel.thumbnails[pictureNo].url,
                                        channel.thumbnails[pictureNo].width,
                                        channel.thumbnails[pictureNo].height);
//      kodi::Log(ADDON_LOG_DEBUG, "Icon Url: %s", iconUrl.c_str());
      kodiChannel.SetIconPath(iconUrl);
      kodiChannel.SetIsHidden(channel.isHidden);
      kodiChannel.SetHasArchive(false);

      results.Add(kodiChannel);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta2::GetChannelStreamProperties(
    const kodi::addon::PVRChannel& channel, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  for (const auto& mychannel : m_channels)
  {
    if (channel.GetUniqueId() == mychannel.iUniqueId)
    {
      ReleaseLock();
      std::string streamUrl = m_basicUrlSelectorService + m_accountPid + "/media/" + mychannel.mediaPath;
      // + "?format=SMIL&formats=MPEG-DASH&tracking=true";
      kodi::Log(ADDON_LOG_DEBUG, "Stream URL -> %s", streamUrl.c_str());

      return SetStreamProperties(properties, streamUrl, true, false, false);
    }
  }
  return PVR_ERROR_FAILED;
}

/*******************************************************************************************************************************************************
*                                                              EPG                                                                                     *
*******************************************************************************************************************************************************/

bool CPVRMagenta2::GetGenre(int& primaryType, int& secondaryType, const std::string& primaryGenre, const std::string& secondaryGenre)
{
  for (const auto& currentgenre : m_genres)
  {
    if (currentgenre.primaryGenre == primaryGenre)
    {
      primaryType = currentgenre.genreType;
      if (currentgenre.genreSubType != -1)
        secondaryType = currentgenre.genreSubType;
      else if (currentgenre.secondaryGenres.size() > 0)
      {
        secondaryType = -1;
        for (int i=0; i < currentgenre.secondaryGenres.size(); i++)
        {
          std::vector<std::string> out;
          tokenize2(secondaryGenre, EPG_STRING_TOKEN_SEPARATOR, out);
          for (int j=0; j < out.size(); j++)
          {
            if (currentgenre.secondaryGenres[i].secondaryGenre == out[j] && secondaryType == -1)
              secondaryType = currentgenre.secondaryGenres[i].secondaryGenreType;
          }
        }
        if (secondaryType == -1)
          secondaryType = 0;
      } else
        secondaryType = 0;
      kodi::Log(ADDON_LOG_DEBUG, "Returning genre %i and subgenre %i for primary %s and secondary %s", primaryType, secondaryType, primaryGenre.c_str(), secondaryGenre.c_str());
      return true;
    }
  }
//  kodi::Log(ADDON_LOG_DEBUG, "Not found primary %s and secondary %s", primaryGenre.c_str(), secondaryGenre.c_str());
  return false;
}

void CPVRMagenta2::SetGenreTypes(const rapidjson::Value& item, std::string& primary, std::string& secondary)
{
  primary = "";
  secondary = "";
  if (item.HasMember("tags"))
  {
    const rapidjson::Value& tags = item["tags"];
    for (rapidjson::SizeType i = 0; i < tags.Size(); i++)
    {
      std::string scheme = Utils::JsonStringOrEmpty(tags[i], "scheme");
      std::string title = Utils::JsonStringOrEmpty(tags[i], "title");
      if (scheme == "genre-primary")
      {
        primary += title;
        primary += EPG_STRING_TOKEN_SEPARATOR;
      } else if (scheme == "genre-secondary")
      {
        secondary += title;
        secondary += EPG_STRING_TOKEN_SEPARATOR;
      } else if (scheme == "category")
      {
        //Todo for later
      } else
      {
        kodi::Log(ADDON_LOG_DEBUG, "Unknown scheme type: %s with title: %s", scheme.c_str(), title.c_str());
      }
    }
    if (!primary.empty())
      primary.erase(primary.end() - 1);
    if (!secondary.empty())
      secondary.erase(secondary.end() - 1);
  }
}

void CPVRMagenta2::AddEPGEntry(const int& channelNumber, const rapidjson::Value& epgItem, kodi::addon::PVREPGTagsResultSet& results)
{
  kodi::addon::PVREPGTag tag;

  unsigned int epg_tag_flags = EPG_TAG_FLAG_UNDEFINED;
  int guid;
  try {
    guid = stoi(Utils::JsonStringOrEmpty(epgItem, "guid").substr(11, std::string::npos), 0, 16);
  }
  catch (const std::exception& e) {
    return;
  }
  tag.SetUniqueBroadcastId(static_cast<unsigned int>(guid));
  tag.SetUniqueChannelId(static_cast<unsigned int>(channelNumber));
  tag.SetTitle(Utils::JsonStringOrEmpty(epgItem, "title"));
  tag.SetPlot(Utils::JsonStringOrEmpty(epgItem, "description"));
  tag.SetPlotOutline(Utils::JsonStringOrEmpty(epgItem, "shortDescription"));

  if (epgItem.HasMember("thumbnails") && epgItem["thumbnails"].GetType() != 0)
  {
    const rapidjson::Value& thumbnails = epgItem["thumbnails"];
    rapidjson::Value::ConstMemberIterator itr = thumbnails.MemberBegin();
    if (itr != thumbnails.MemberEnd())
      ++itr;
    if (itr != thumbnails.MemberEnd())
    {
      const rapidjson::Value& thumbnailsItem = (itr->value);
      if (!thumbnailsItem.IsNull())
      {
        int width = Utils::JsonIntOrZero(thumbnailsItem, "width");
        int height = Utils::JsonIntOrZero(thumbnailsItem, "height");
        if ((width == 0) && (height == 0))
          tag.SetIconPath(Utils::JsonStringOrEmpty(thumbnailsItem, "url"));
        else
          tag.SetIconPath(GetNgissUrl(Utils::JsonStringOrEmpty(thumbnailsItem, "url"), width, height));
      }
    }
  }

  int seasonNum = Utils::JsonIntOrZero(epgItem, "tvSeasonNumber");
  if (seasonNum != 0)
  {
    tag.SetSeriesNumber(seasonNum);
    epg_tag_flags += EPG_TAG_FLAG_IS_SERIES;
  }
  int episodeNum = Utils::JsonIntOrZero(epgItem, "tvSeasonEpisodeNumber");
  if (episodeNum != 0)
    tag.SetEpisodeNumber(episodeNum);
  tag.SetYear(Utils::JsonIntOrZero(epgItem, "year"));
  tag.SetEpisodeName(Utils::JsonStringOrEmpty(epgItem, "secondaryTitle"));
  std::string seriesId = Utils::JsonStringOrEmpty(epgItem, "seriesId");
  if (!seriesId.empty())
    tag.SetSeriesLink(seriesId);

  if (epgItem.HasMember("ratings") && epgItem["ratings"].GetType() != 0)
  {
    const rapidjson::Value& ratings = epgItem["ratings"];
    if (ratings.Size() > 0) {
      std::string ratingStr = Utils::JsonStringOrEmpty(ratings[0], "rating");
      int rating = 0;
      try {
        rating = stoi(ratingStr);
      } catch (const std::exception& e) {}
      if (rating > 0)
        tag.SetParentalRating(rating);
    }
  }

  if (epgItem.HasMember("dt$originalIds") && epgItem["dt$originalIds"].GetType() != 0)
  {
    const rapidjson::Value& originalIds = epgItem["dt$originalIds"];
    std::string imdbNumber = Utils::JsonStringOrEmpty(originalIds, "imdb");
    if (!imdbNumber.empty())
      tag.SetIMDBNumber(imdbNumber);
  }

  if (epgItem.HasMember("credits") && epgItem["credits"].GetType() != 0) {
    const rapidjson::Value& credits = epgItem["credits"];
    std::string cast = "";
    std::string director = "";
    std::string writer = "";
    for (rapidjson::SizeType i = 0; i < credits.Size(); i++)
    {
      std::string creditType = Utils::JsonStringOrEmpty(credits[i], "creditType");
      if (creditType == "DIRECTOR")
      {
        if (director != "")
          director += EPG_STRING_TOKEN_SEPARATOR;
        director += Utils::JsonStringOrEmpty(credits[i], "personName");
      } else if (creditType == "SCRIPTWRITER")
      {
        if (writer != "")
          writer += EPG_STRING_TOKEN_SEPARATOR;
        writer += Utils::JsonStringOrEmpty(credits[i], "personName");
      } else if ((creditType == "ACTOR") || (creditType == "AD6"))
      {
        if (cast != "")
          cast += EPG_STRING_TOKEN_SEPARATOR;
        cast += Utils::JsonStringOrEmpty(credits[i], "personName");
      } else if (creditType == "PRODUCER")
      {

      } else
      {
        kodi::Log(ADDON_LOG_DEBUG, "Unknown Credit Type: %s Person Name: %s", creditType.c_str(), Utils::JsonStringOrEmpty(credits[i], "personName").c_str());
      }
    }
    tag.SetCast(cast);
    tag.SetDirector(director);
    tag.SetWriter(writer);
  }

  std::string genre_primary = "";
  std::string genre_secondary = "";
  SetGenreTypes(epgItem, genre_primary, genre_secondary);
  int primaryType;
  int secondaryType;
  if (GetGenre(primaryType, secondaryType, genre_primary, genre_secondary))
  {
    tag.SetGenreType(primaryType);
    tag.SetGenreSubType(secondaryType);
  } else
  {
    kodi::Log(ADDON_LOG_DEBUG, "Primary Genres: %s", genre_primary.c_str());
    kodi::Log(ADDON_LOG_DEBUG, "Secondary Genres: %s", genre_secondary.c_str());
    tag.SetGenreType(EPG_GENRE_USE_STRING);
    tag.SetGenreDescription(genre_secondary);
  }

  tag.SetFlags(epg_tag_flags);

  if (epgItem.HasMember("listings") && epgItem["listings"].GetType() != 0) {
    const rapidjson::Value& listings = epgItem["listings"];
    for (rapidjson::SizeType i = 0; i < listings.Size(); i++) {
      tag.SetStartTime((time_t) (Utils::JsonInt64OrZero(listings[i], "startTime") / 1000));
      tag.SetEndTime((time_t) (Utils::JsonInt64OrZero(listings[i], "endTime") / 1000));
      results.Add(tag);
    }
  }
}

bool CPVRMagenta2::GetEPGFeed(const int& channelNumber, const std::string& baseUrl, kodi::addon::PVREPGTagsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  rapidjson::Document doc;
  if (!GetPostJson(baseUrl, "", doc)) {
    return false;
  }

  if (!doc.HasMember("entries") || (doc["entries"].GetType() == 0))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get EPG feed");
    return false;
  }

  std::string guids = "";
  int entryCount = 0;
  const rapidjson::Value& entries = doc["entries"];
  for (rapidjson::SizeType i = 0; i < entries.Size(); i++)
  {
    if (!entries[i].HasMember("listings") || (entries[i]["listings"].GetType() == 0))
    {
      kodi::Log(ADDON_LOG_ERROR, "Failed to get EPG listings");
      return false;
    }
    const rapidjson::Value& listings = entries[i]["listings"];
    for (rapidjson::SizeType j = 0; j < listings.Size(); j++)
    {
        if (listings[j].HasMember("program") && listings[j]["program"].GetType() != 0) {
        const rapidjson::Value& program = listings[j]["program"];
        guids += Utils::JsonStringOrEmpty(program, "guid") + "|";
        entryCount++;
      }
      if ((entryCount == 300) || (j == listings.Size()-1))
      {
        guids.erase(guids.end() - 1);
      //  kodi::Log(ADDON_LOG_DEBUG, "Guids found %s", guids.c_str());

        std::string programsUrl = m_allProgramsFeedUrl + "?form=cjson" +
                                                         "&byGuid=" + Utils::UrlEncode(guids) +
                                                         "&range=1-" + std::to_string(entryCount) +
                                                         "&fields=guid,title,description,listings.startTime,"
                                                         "listings.endTime,thumbnails,tvSeasonNumber,tvSeasonEpisodeNumber,"
                                                         "year,secondaryTitle,seriesId,ratings,dt$originalIds,"
                                                         "credits.creditType,credits.personName,shortDescription,tags"; //programType

        rapidjson::Document doc2;
        if (!GetPostJson(programsUrl, "", doc2)) {
          return false;
        }

        if (!doc2.HasMember("entries") || (doc2["entries"].GetType() == 0))
        {
          kodi::Log(ADDON_LOG_ERROR, "Failed to get Programs feed");
          return false;
        }

        const rapidjson::Value& entries2 = doc2["entries"];
        for (rapidjson::SizeType k = 0; k < entries2.Size(); k++)
        {
          AddEPGEntry(channelNumber, entries2[k], results);
        }
        guids = "";
        entryCount = 0;
      }
    }
  }
  return true;
}

PVR_ERROR CPVRMagenta2::GetEPGForChannel(int channelUid,
                                         time_t start,
                                         time_t end,
                                         kodi::addon::PVREPGTagsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
 
  kodi::Log(ADDON_LOG_DEBUG, "Start %u End %u", start, end);

  std::string baseUrl = m_allChannelSchedulesFeed + "?form=cjson&byLocationId=" + Utils::UrlEncode(m_locationIdUri) +
                                                    "&byListingTime=" + Utils::UrlEncode(Utils::TimeToString2(start) + "~" + Utils::TimeToString2(end)) +
                                                    "&byChannelNumber=" + std::to_string(channelUid) +
                                                    "&range=1-1" +
                                                    "&fields=listings.program.guid,listings.guid";
  GetEPGFeed(channelUid, baseUrl, results);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta2::IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& bIsPlayable)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  time_t currentTime = time(NULL);
  time_t startTime=tag.GetStartTime();
  time_t endTime=tag.GetEndTime();
  bIsPlayable= (currentTime >= startTime && currentTime <= endTime);
  return PVR_ERROR_NO_ERROR;

  // gemx: this makes scrolling in epg horribly slow
  /*bIsPlayable = false;

  std::stringstream ss;
  ss<< std::hex << tag.GetUniqueBroadcastId(); // int decimal_value
  std::string epgId ( ss.str() );

  while (epgId.size() < 8)
    epgId.insert(0, "0");

  kodi::Log(ADDON_LOG_DEBUG, "Checking if EPGTag with UID: %s is playable", epgId.c_str());

  std::string programsUrl = m_allProgramsFeedUrl + "?form=cjson" +
                                                   "&byGuid=" + "telekom.de-" + epgId +
                                                   "&range=1-1" +
                                                   "&fields=media.publicUrl,media.availableDate," +
                                                   "media.expirationDate"; //programType

  rapidjson::Document doc;
  if (!GetPostJson(programsUrl, "", doc)) {
    return PVR_ERROR_NO_ERROR;
  }

  if (!doc.HasMember("entries"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get Programs feed");
    return PVR_ERROR_NO_ERROR;
  }

  const rapidjson::Value& entries = doc["entries"];

  if (entries.Size() != 1)
    return PVR_ERROR_NO_ERROR;

  if (!entries[0].HasMember("media"))
    return PVR_ERROR_NO_ERROR;

  const rapidjson::Value& media = entries[0]["media"];

  if (media.Size() == 0)
    return PVR_ERROR_NO_ERROR;

  if (!media[0].HasMember("publicUrl") || !media[0].HasMember("availableDate") || !media[0].HasMember("expirationDate"))
    return PVR_ERROR_NO_ERROR;

  time_t availableDate = (time_t) (Utils::JsonInt64OrZero(media[0], "availableDate") / 1000);
  time_t expirationDate = (time_t) (Utils::JsonInt64OrZero(media[0], "expirationDate") / 1000);
  std::string publicUrl = Utils::JsonStringOrEmpty(media[0], "publicUrl");
  auto current_time = time(NULL);

  if (current_time > availableDate && current_time < expirationDate && !publicUrl.empty())
  {
    kodi::Log(ADDON_LOG_DEBUG, "Found public URL: %s", publicUrl.c_str());
    bIsPlayable = true;
  }

  return PVR_ERROR_NO_ERROR;*/
}

PVR_ERROR CPVRMagenta2::GetEPGTagStreamProperties(
    const kodi::addon::PVREPGTag& tag, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::stringstream ss;
  ss<< std::hex << tag.GetUniqueBroadcastId(); // int decimal_value
  std::string epgId ( ss.str() );

  while (epgId.size() < 8)
    epgId.insert(0, "0");

  kodi::Log(ADDON_LOG_DEBUG, "Get stream parameters for %s", epgId.c_str());

  std::string programsUrl = m_allProgramsFeedUrl + "?form=cjson" +
                                                   "&byGuid=" + "telekom.de-" + epgId +
                                                   "&range=1-1" +
                                                   "&fields=media.publicUrl"; //programType

  rapidjson::Document doc;
  if (!GetPostJson(programsUrl, "", doc)) {
    return PVR_ERROR_FAILED;
  }

  if (!doc.HasMember("entries"))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get Programs feed");
    return PVR_ERROR_FAILED;
  }

  const rapidjson::Value& entries = doc["entries"];

  if (entries.Size() != 1)
    return PVR_ERROR_FAILED;

  if (!entries[0].HasMember("media"))
    return PVR_ERROR_FAILED;

  const rapidjson::Value& media = entries[0]["media"];

  if (media.Size() == 0)
    return PVR_ERROR_FAILED;

  if (!media[0].HasMember("publicUrl"))
    return PVR_ERROR_FAILED;

  std::string publicUrl = Utils::JsonStringOrEmpty(media[0], "publicUrl");
  if (!publicUrl.empty())
  {
    kodi::Log(ADDON_LOG_DEBUG, "Timeshift URL: %s", publicUrl.c_str());
    return SetStreamProperties(properties, publicUrl, false, true, false);
  }

  return PVR_ERROR_FAILED;
}

/*******************************************************************************************************************************************************
*                                                              Groups                                                                                  *
*******************************************************************************************************************************************************/

PVR_ERROR CPVRMagenta2::GetChannelGroupsAmount(int& amount)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  amount = static_cast<int>(m_categories.size());
  std::string amount_str = std::to_string(amount);
  kodi::Log(ADDON_LOG_DEBUG, "Groups Amount: [%s]", amount_str.c_str());

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta2::GetChannelGroups(bool bRadio, kodi::addon::PVRChannelGroupsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  for (const auto& category : m_categories)
  {
    kodi::addon::PVRChannelGroup kodiGroup;

    if (!bRadio && category.level == 0 && !category.channelUids.empty())
    {
      kodiGroup.SetPosition(category.order);
      kodiGroup.SetIsRadio(false); // is no radio group
      kodiGroup.SetGroupName(category.description);

      results.Add(kodiGroup);
      kodi::Log(ADDON_LOG_DEBUG, "Group added: %s at position %u level %u", category.description.c_str(), category.order, category.level);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta2::GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group,
                                           kodi::addon::PVRChannelGroupMembersResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  for (const auto& cgroup : m_categories)
  {
    if (cgroup.description != group.GetGroupName())
      continue;

    unsigned int position = 0;
    for (const int& channelid : cgroup.channelUids)
    {
      kodi::addon::PVRChannelGroupMember kodiGroupMember;

      kodiGroupMember.SetGroupName(cgroup.description);
      kodiGroupMember.SetChannelUniqueId(static_cast<unsigned int>(channelid));
      kodiGroupMember.SetChannelNumber(static_cast<unsigned int>(++position));

      results.Add(kodiGroupMember);
    }
    return PVR_ERROR_NO_ERROR;
  }

  return PVR_ERROR_NO_ERROR;
}

/*******************************************************************************************************************************************************
*                                                              Timers                                                                                  *
*******************************************************************************************************************************************************/
namespace
{
struct TimerType : kodi::addon::PVRTimerType
{
  TimerType(int defaultLifetimeValue,
            unsigned int id,
            unsigned int attributes,
            const std::string& description,
            const std::vector<kodi::addon::PVRTypeIntValue>& lifetimeValues = std::vector<kodi::addon::PVRTypeIntValue>())
 {
    SetId(id);
    SetAttributes(attributes);
    SetDescription(description);
    SetLifetimes(lifetimeValues, defaultLifetimeValue);
  }
};
} // unnamed namespace

int CPVRMagenta2::CountTimersRecordings(const bool& isRecording)
{
  std::string url = m_pvrBaseUrl + "/get-recordings?limit=500";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return 0;
  }

  if (!doc.HasMember("recordings"))
    return 0;

  int count = 0;
  const rapidjson::Value& recordings = doc["recordings"];

  for (rapidjson::SizeType i = 0; i < recordings.Size(); i++)
  {
    std::string recordingStatus = Utils::JsonStringOrEmpty(recordings[i], "recordingStatus");
    if (isRecording)
    {
      if (recordingStatus == "RECORDING" || recordingStatus == "GENERATED")
        count++;
    } else
    {
      if (recordingStatus == "SCHEDULED")
        count++;
    }
  }
  return count;
}

PVR_ERROR CPVRMagenta2::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  /* PVR_Timer.iLifetime values and presentation.*/
  std::vector<kodi::addon::PVRTypeIntValue> lifetimeValues = 
  {
    {MAGENTA_RECORDING_DELETE_MODE_WHEN_FULL, kodi::addon::GetLocalizedString(30050)},
    {MAGENTA_RECORDING_DELETE_MODE_MANUAL, kodi::addon::GetLocalizedString(30051)}
  };
  

  unsigned int TIMER_ONCE_EPG_ATTRIBS =
       PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE |
       PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN;

   /* One-shot epg based */
  types.emplace_back(TimerType(
    0, // delete mode when full
    TIMER_ONCE_EPG,
    TIMER_ONCE_EPG_ATTRIBS,
    "Einzelaufnahme",
    lifetimeValues));

  unsigned int TIMER_SERIES_EPG_ATTRIBS =
      PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN |
      PVR_TIMER_TYPE_IS_REPEATING;

  /* Series epg based */
  types.emplace_back(TimerType(
    0, // delete mode when full
    TIMER_SERIES_EPG,
    TIMER_SERIES_EPG_ATTRIBS,
    "Serienaufnahme",
     lifetimeValues));

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta2::GetTimersAmount(int& amount)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  amount = static_cast<int>(CountTimersRecordings(false));
  std::string amount_str = std::to_string(amount);
  kodi::Log(ADDON_LOG_DEBUG, "Timers Amount: [%s]", amount_str.c_str());

  return PVR_ERROR_NO_ERROR;
}

std::string serializeDocumentToString(const rapidjson::Document& doc) {
    // StringBuffer will hold the serialized JSON
    rapidjson::StringBuffer buffer;

    // Writer writes JSON into the buffer
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

    // Serialize the document into the writer
    if (!doc.Accept(writer)) {
        throw std::runtime_error("Failed to serialize RapidJSON document.");
    }

    // Return as std::string
    return buffer.GetString();
}

PVR_ERROR CPVRMagenta2::AddSeriesPVRTimer(kodi::addon::PVRTimersResultSet& results)
{
  if (kodi::vfs::FileExists("/sdcard/safemode.txt"))
  {
    kodi::Log(ADDON_LOG_DEBUG, "Not running AddSeriesPVRTImer because of safe mode");
    return PVR_ERROR_NO_ERROR;
  }
     /*
  Get Series and add them

  When getting the recordings match recording.series.guid with scheduledSeries.seriesGuid
  https://audience.npvr.eu.theplatform.com/npvr-audience/2709353023/get-scheduled-series-list?limit=500&offset=1
  [ {  "seriesGuid" : "telekom.de-24834",  "firstRunOnly" : false,  "stationReferences" : [ "vox_hd" ],  "title" : "Die Höhle der Löwen",  "titleLocalized" : {    "de" : "Die Höhle der Löwen"  },  "startOffsetSeconds" : 60,  "endOffsetSeconds" : 300,  "recordWindowStart" : "00:00:00Z",  "recordWindowEnd" : "24:00:00Z",  "keepLastRecordings" : 0,  "recordFromSeason" : 0,  "recordFromSeasonEpisode" : 0}]
} ]*/
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
        
  m_seriesTimers.clear();
  std::string url = m_pvrBaseUrl + "/get-scheduled-series-list?limit=500";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) 
  {
    return PVR_ERROR_NO_ERROR;
  }
  
 if (!doc.IsArray()) 
 {
    kodi::Log(ADDON_LOG_ERROR, "Invalid json. Expected an array as root.");
    return PVR_ERROR_FAILED;
  }

  const rapidjson::Value& series = doc.GetArray();

  for (rapidjson::SizeType i = 0; i < series.Size(); i++)
  {
    kodi::Log(ADDON_LOG_DEBUG,"Loop %i",i);
    
    kodi::addon::PVRTimer kodiTimer;
    kodiTimer.SetTimerType(TIMER_SERIES_EPG);
    kodiTimer.SetState(PVR_TIMER_STATE_SCHEDULED);
    kodiTimer.SetTitle(Utils::JsonStringOrEmpty(series[i], "title"));
    int guid = stoi(Utils::JsonStringOrEmpty(series[i], "seriesGuid").substr(11, std::string::npos), 0, 16);
    kodiTimer.SetClientIndex(guid);
    kodiTimer.SetEPGSearchString(Utils::JsonStringOrEmpty(series[i], "seriesGuid")); //GEMX: also save the seriesId somewhere in case we need it
    //tagGroup.SetClientChannelUid(timerGroup.channelId);
    kodiTimer.SetMarginStart(Utils::JsonIntOrZero(series[i],"startOffsetSeconds")/60);
    kodiTimer.SetMarginEnd(Utils::JsonIntOrZero(series[i],"endOffsetSeconds")/60);
    kodiTimer.SetStartAnyTime(true);
    kodiTimer.SetEndAnyTime(true);
    kodiTimer.SetSeriesLink(Utils::JsonStringOrEmpty(series[i], "seriesGuid"));
    
    results.Add(kodiTimer);
    m_seriesTimers.emplace_back(kodiTimer);
    kodi::Log(ADDON_LOG_DEBUG,"series added as %s with seriesGuid %s",kodiTimer.GetTitle().c_str(), kodiTimer.GetEPGSearchString().c_str());
  }

  return PVR_ERROR_NO_ERROR;
}

std::string CPVRMagenta2::GetSeriesGuidFromSeriesIdUrl(std::string url)
{
  size_t pos = url.rfind('/');
  return (pos != std::string::npos) ? url.substr(pos + 1) : url;
}

int CPVRMagenta2::GetSeriesTimerIdBySeriesId(std::string seriesGuid)
{
  kodi::Log(ADDON_LOG_DEBUG, "Searching for series with guid %s  ...",seriesGuid.c_str());
  for (auto& timer : m_seriesTimers)
  {
    if (timer.GetEPGSearchString() == seriesGuid)
    {
      int idx=timer.GetClientIndex();
      kodi::Log(ADDON_LOG_DEBUG, "Found series  id %s - index: %i",seriesGuid.c_str(),idx);
      return idx;
    }
  }
  kodi::Log(ADDON_LOG_DEBUG, "Didn't find a series with id %s ...",seriesGuid.c_str());
  return PVR_TIMER_NO_PARENT;
}

PVR_ERROR CPVRMagenta2::AddOncePVRTimer(kodi::addon::PVRTimersResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  std::string url = m_pvrBaseUrl + "/get-recordings?limit=500";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return PVR_ERROR_FAILED;
  }

  if (!doc.HasMember("recordings"))
    return PVR_ERROR_FAILED;

  const rapidjson::Value& recordings = doc["recordings"];

  for (rapidjson::SizeType i = 0; i < recordings.Size(); i++)
  {
    std::string recordingStatus = Utils::JsonStringOrEmpty(recordings[i], "recordingStatus");
    if (recordings[i].HasMember("program") && recordings[i].HasMember("listing") && recordingStatus=="SCHEDULED")
    {
      kodi::addon::PVRTimer kodiTimer;
      const rapidjson::Value& recordingItem=recordings[i];
      const rapidjson::Value& program = recordings[i]["program"];
      const rapidjson::Value& listing = recordings[i]["listing"];

      int guid = stoi(Utils::JsonStringOrEmpty(program, "guid").substr(11, std::string::npos), 0, 16);
      kodiTimer.SetClientIndex(guid);
      kodiTimer.SetEPGUid(guid);
      kodiTimer.SetState(PVR_TIMER_STATE_SCHEDULED);
      kodiTimer.SetTimerType(TIMER_ONCE_EPG);
      kodiTimer.SetTitle(Utils::JsonStringOrEmpty(program, "title"));
      kodiTimer.SetParentClientIndex(GetSeriesTimerIdBySeriesId(Utils::JsonStringOrEmpty(recordingItem["series"], "guid"))); 
      kodiTimer.SetStartTime(Utils::StringToTime2(Utils::JsonStringOrEmpty(recordingItem,"startDateTime")));
      kodiTimer.SetEndTime(Utils::StringToTime2(Utils::JsonStringOrEmpty(recordingItem,"endDateTime")));
      kodiTimer.SetEPGSearchString(Utils::JsonStringOrEmpty(listing, "guid")); 
      time_t expirationDateTime = Utils::StringToTime2(Utils::JsonStringOrEmpty(recordingItem, "expirationDateTime"));
      kodiTimer.SetLifetime(static_cast<int>((expirationDateTime - time(NULL))/(60*60*24)));
      kodiTimer.SetMarginStart(Utils::JsonIntOrZero(recordingItem,"startOffsetSeconds")/60);
      kodiTimer.SetMarginEnd(Utils::JsonIntOrZero(recordingItem,"endOffsetSeconds")/60);

      std::string id=Utils::JsonStringOrEmpty(listing, "stationId");
      for (const auto& thisChannel : m_channels)
      {
        if (thisChannel.stationsId == id)
        {
          kodiTimer.SetClientChannelUid(thisChannel.iUniqueId);
          break;
        }
      }
 
      std::string genre_primary = "";
      std::string genre_secondary = "";
      SetGenreTypes(program, genre_primary, genre_secondary);
      int primaryType;
      int secondaryType;
      if (GetGenre(primaryType, secondaryType, genre_primary, genre_secondary))
      {
        kodiTimer.SetGenreType(primaryType);
        kodiTimer.SetGenreSubType(secondaryType);
      } 
      else
      {
        kodi::Log(ADDON_LOG_DEBUG, "Primary Genres: %s", genre_primary.c_str());
        kodi::Log(ADDON_LOG_DEBUG, "Secondary Genres: %s", genre_secondary.c_str());
        kodiTimer.SetGenreType(EPG_GENRE_USE_STRING);
      }
      results.Add(kodiTimer);
      kodi::Log(ADDON_LOG_DEBUG, "Timer added: %s", kodiTimer.GetTitle().c_str());
    }
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta2::GetTimers(kodi::addon::PVRTimersResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  PVR_ERROR result = AddSeriesPVRTimer(results);
  if (result!=PVR_ERROR_NO_ERROR)
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to load series timer. Only the once timers will be available");
  }

  return AddOncePVRTimer(results);
}

std::string GetChannelGuidFromListingGuid(std::string listingGuid)
{
    size_t pos = listingGuid.rfind('_');
    return (pos != std::string::npos) ? listingGuid.substr(0, pos) : listingGuid;
}

PVR_ERROR CPVRMagenta2::GetListingAndSeriesGuidFromAllChannelSchedulesFeed(const kodi::addon::PVRTimer timer, std::string& listingGuid, std::string& seriesGuid, std::string channelGuid)
{
    // we need to add a margin because the times get rounded to the hour
  int thirtyMinutes=30*60;
  int startWindow=timer.GetStartTime()-thirtyMinutes;
  int endWindow=timer.GetEndTime()+thirtyMinutes;
  std::string baseUrl = m_allChannelSchedulesFeed + "?form=cjson&byLocationId=" + Utils::UrlEncode(m_locationIdUri) +
                                                "&byListingTime=" + Utils::UrlEncode(Utils::TimeToString2(startWindow) + "~" + Utils::TimeToString2(endWindow)) +
                                                "&byChannelNumber=" + std::to_string(timer.GetClientChannelUid()) +
                                                "&range=1-1" +
                                                "&fields=listings.program.guid,listings.guid,listings.seriesId";
													
  rapidjson::Document doc;
  if (!GetPostJson(baseUrl, "", doc)) 
  {
    return PVR_ERROR_FAILED;
  }

  if (!doc.HasMember("entries") || (doc["entries"].GetType() == 0))
  {
    kodi::Log(ADDON_LOG_ERROR, "unexpected response for %s",baseUrl.c_str());
    return PVR_ERROR_FAILED;
  }

  if (doc["entries"].Size()==0)
  {
    kodi::Log(ADDON_LOG_ERROR, "no entries returned from allChannelsSchedulesFeed");
    return PVR_ERROR_FAILED;
  }

  const rapidjson::Value& channelEntry = doc["entries"][0];
  const rapidjson::Value& listings = channelEntry["listings"];

  std::string hexEPGUid=Utils::IntToHexString(timer.GetEPGUid());
  kodi::Log(ADDON_LOG_DEBUG, "EPG hex guid is %s",hexEPGUid.c_str());

  channelGuid=GetChannelGuidFromListingGuid(Utils::JsonStringOrEmpty(listings[0],"guid"));
  kodi::Log(ADDON_LOG_DEBUG, "channel guid is %s",channelGuid.c_str());

  listingGuid=channelGuid+"_"+hexEPGUid;
  kodi::Log(ADDON_LOG_DEBUG, "listing guid is %s",listingGuid.c_str());

  seriesGuid="";

  if (timer.GetTimerType()==TIMER_SERIES_EPG)
  {
    for (rapidjson::SizeType i = 0; i < listings.Size(); i++)
    {
      if (listingGuid==Utils::JsonStringOrEmpty(listings[i],"guid"))
      {
        seriesGuid = GetSeriesGuidFromSeriesIdUrl(Utils::JsonStringOrEmpty(listings[i],"seriesId"));
        break;
      }
    }
    if (seriesGuid=="")
    {
      kodi::Log(ADDON_LOG_ERROR,"no listing with guid %s found in %s",listingGuid.c_str(), Utils::SerializeJsonValue(channelEntry).c_str());    
      return PVR_ERROR_FAILED;
    }
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta2::AddTimer(const kodi::addon::PVRTimer& timer)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

   kodi::Log(ADDON_LOG_DEBUG,"AddTimer %s for epg %i marging start %i, margin end %i, channelUid %i",timer.GetTitle().c_str(),timer.GetEPGUid(), timer.GetMarginStart(),timer.GetMarginEnd(),timer.GetClientChannelUid());

  std::string listingGuid, seriesGuid, channelGuid;
  PVR_ERROR result = GetListingAndSeriesGuidFromAllChannelSchedulesFeed(timer, listingGuid, seriesGuid, channelGuid);
  if (result!=PVR_ERROR_NO_ERROR)
  {
    return result;
  }

  std::string url=m_pvrBaseUrl;
  if (timer.GetTimerType()==TIMER_SERIES_EPG)
  {
    url+="/schedule-recording-for-series/"+seriesGuid;
  }
  else
  {
    url+="/schedule-recording-for-listing/"+listingGuid;
  }
  int marginStart=timer.GetMarginStart()*60;
  int marginEnd=timer.GetMarginEnd()*60;
  std::string body="{\"stationReferences\":[\""+channelGuid+"\"],\"endOffsetSeconds\":"+std::to_string(marginEnd)+",\"startOffsetSeconds\":"+std::to_string(marginStart)+"}";
  kodi::Log(ADDON_LOG_DEBUG,"url is %s and body is %s",url.c_str(), body.c_str());

  rapidjson::Document respDoc;
  if (!GetPostJson(url, body, respDoc)) 
  {
    return PVR_ERROR_FAILED;
  }

  usleep(2000000); //Sometimes magenta needs some time to propagate the info. Wait 2 seconds
  if (timer.GetTimerType()==TIMER_SERIES_EPG)
  {
    kodi::Log(ADDON_LOG_DEBUG,"Added TIMER_SERIES_EPG");
    kodi::QueueNotification(QUEUE_INFO, "Timer", "Serienaufnahme hinzugefügt");
  }
  else
  {
    kodi::Log(ADDON_LOG_DEBUG,"Added TIMER_ONCE_EPG");
    kodi::QueueNotification(QUEUE_INFO, "Timer", "Einzelaufnahme hinzugefügt");
  }
   m_instancePVRClient->TriggerTimerUpdate(); 
 return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta2::UpdateTimer(const kodi::addon::PVRTimer& timer)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string listingGuid, seriesGuid, channelGuid;
  PVR_ERROR result = GetListingAndSeriesGuidFromAllChannelSchedulesFeed(timer, listingGuid, seriesGuid, channelGuid);
  if (result!=PVR_ERROR_NO_ERROR)
  {
    return result;
  }

  std::string url=m_pvrBaseUrl;
  if (timer.GetTimerType()==TIMER_SERIES_EPG)
  {
    url+="/update-recording-for-series/"+seriesGuid;
  }
  else
  {
    url+="/update-recording-for-listing/"+listingGuid;
  }
  int marginStart=timer.GetMarginStart()*60;
  int marginEnd=timer.GetMarginEnd()*60;
  std::string body="{\"stationReferences\":[\""+channelGuid+"\"],\"endOffsetSeconds\":"+std::to_string(marginEnd)+",\"startOffsetSeconds\":"+std::to_string(marginStart)+"}";
  kodi::Log(ADDON_LOG_DEBUG,"url is %s and body is %s",url.c_str(), body.c_str());

  rapidjson::Document respDoc;
  if (!GetPostJson(url, body, respDoc)) 
  {
    return PVR_ERROR_FAILED;
  }

  usleep(2000000); //Sometimes magenta needs some time to propagate the info. Wait 2 seconds
  if (timer.GetTimerType()==TIMER_SERIES_EPG)
  {
    kodi::Log(ADDON_LOG_DEBUG,"Updated TIMER_SERIES_EPG");
    kodi::QueueNotification(QUEUE_INFO, "Timer", "Serienaufnahme aktualisiert");
  }
  else
  {
    kodi::Log(ADDON_LOG_DEBUG,"Updated TIMER_ONCE_EPG");
    kodi::QueueNotification(QUEUE_INFO, "Timer", "Einzelaufnahme aktualisiert");
  }
  m_instancePVRClient->TriggerTimerUpdate();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta2::DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete)
{
    kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

    std::string url="";
    useconds_t delay=2000000;
    if (timer.GetTimerType()==TIMER_SERIES_EPG)
    {
       url = m_pvrBaseUrl + "/cancel-schedule-for-series/"+timer.GetEPGSearchString();
       // cancelling schedules takes a bit longer - Need some way to detect if its really deleted
       delay=2000000;

    }
    else
    {
      url = m_pvrBaseUrl + "/delete-recording-for-listing/"+timer.GetEPGSearchString();
    }

    if (SendDeleteRequest(url)) 
    {
      usleep(delay); //Sometimes magenta needs some time to propagate the info. Wait 2 seconds
      kodi::QueueNotification(QUEUE_INFO, "Timer", "Timer gelöscht");
      m_instancePVRClient->TriggerTimerUpdate();
      return PVR_ERROR_NO_ERROR;
    }
    else
    {
      return PVR_ERROR_FAILED;
    }


}
/*******************************************************************************************************************************************************
*                                                              Recordings                                                                              *
*******************************************************************************************************************************************************/

PVR_ERROR CPVRMagenta2::GetRecordingsAmount(bool deleted, int& amount)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  amount = static_cast<int>(CountTimersRecordings(true));
//  amount += GetGroupRecordingsAmount();
  std::string amount_str = std::to_string(amount);
  kodi::Log(ADDON_LOG_DEBUG, "Recordings Amount: [%s]", amount_str.c_str());

  return PVR_ERROR_NO_ERROR;
}

void CPVRMagenta2::FillPVRRecording(const rapidjson::Value& recordingItem, kodi::addon::PVRRecording& kodiRecording)
{
  const rapidjson::Value& program = recordingItem["program"];
  const rapidjson::Value& listing = recordingItem["listing"];
  //kodiRecording.SetRecordingId(Utils::JsonStringOrEmpty(recordingItem, "id"));
  // we need the listing guid because that is needed for deletion of a recording
  kodiRecording.SetRecordingId(Utils::JsonStringOrEmpty(listing, "guid"));
  kodiRecording.SetTitle(Utils::JsonStringOrEmpty(program, "title"));
  kodiRecording.SetYear(Utils::JsonIntOrZero(program, "year"));
  kodiRecording.SetPlot(Utils::JsonStringOrEmpty(program, "description"));
  kodiRecording.SetPlotOutline(Utils::JsonStringOrEmpty(program, "shortDescription"));
  kodiRecording.SetDuration(static_cast<int>(Utils::JsonDoubleOrZero(program, "runtime")));
  time_t expirationDateTime = Utils::StringToTime2(Utils::JsonStringOrEmpty(recordingItem, "expirationDateTime"));

  kodiRecording.SetLifetime(static_cast<int>((expirationDateTime - time(NULL))/(60*60*24)));
  int epgGuid = stoi(Utils::JsonStringOrEmpty(program, "guid").substr(11, std::string::npos), 0, 16);
  kodiRecording.SetEPGEventId(epgGuid);
  kodiRecording.SetRecordingTime(Utils::StringToTime2(Utils::JsonStringOrEmpty(recordingItem,"startDateTime")));
  kodiRecording.SetChannelType(PVR_RECORDING_CHANNEL_TYPE_TV);

  std::string channelName;
  if (GetChannelNamebyId(Utils::JsonStringOrEmpty(listing, "stationId"), channelName))
    kodiRecording.SetChannelName(channelName);

  std::string genre_primary = "";
  std::string genre_secondary = "";
  SetGenreTypes(program, genre_primary, genre_secondary);
  int primaryType;
  int secondaryType;
  if (GetGenre(primaryType, secondaryType, genre_primary, genre_secondary))
  {
    kodiRecording.SetGenreType(primaryType);
    kodiRecording.SetGenreSubType(secondaryType);
  } else
  {
    kodi::Log(ADDON_LOG_DEBUG, "Primary Genres: %s", genre_primary.c_str());
    kodi::Log(ADDON_LOG_DEBUG, "Secondary Genres: %s", genre_secondary.c_str());
    kodiRecording.SetGenreType(EPG_GENRE_USE_STRING);
    kodiRecording.SetGenreDescription(genre_secondary);
  }

  if (program.HasMember("thumbnails"))
  {
    const rapidjson::Value& thumbnails = program["thumbnails"];
    rapidjson::Value::ConstMemberIterator itr = thumbnails.MemberBegin();
    ++itr;
    if (itr != thumbnails.MemberEnd())
    {
      const rapidjson::Value& thumbnailsItem = (itr->value);
      if (!thumbnailsItem.IsNull())
      {
        int width = Utils::JsonIntOrZero(thumbnailsItem, "width");
        int height = Utils::JsonIntOrZero(thumbnailsItem, "height");
        std::string iconUrl = Utils::JsonStringOrEmpty(thumbnailsItem, "url");
        if ((width == 0) && (height == 0))
        {
          kodiRecording.SetIconPath(iconUrl);
          kodiRecording.SetFanartPath(iconUrl);
          kodiRecording.SetThumbnailPath(iconUrl);
        }
        else
        {
          kodiRecording.SetIconPath(GetNgissUrl(iconUrl, width, height));
          kodiRecording.SetFanartPath(GetNgissUrl(iconUrl, width, height));
          kodiRecording.SetThumbnailPath(GetNgissUrl(iconUrl, width, height));
        }
      }
    }
  }
}

PVR_ERROR CPVRMagenta2::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string url = m_pvrBaseUrl + "/get-recordings?limit=500";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return PVR_ERROR_FAILED;
  }

  if (!doc.HasMember("recordings"))
    return PVR_ERROR_FAILED;

  const rapidjson::Value& recordings = doc["recordings"];

  for (rapidjson::SizeType i = 0; i < recordings.Size(); i++)
  {
    if (recordings[i].HasMember("program") && recordings[i].HasMember("listing"))
    {
      std::string recordingStatus = Utils::JsonStringOrEmpty(recordings[i], "recordingStatus");
      if (recordingStatus == "RECORDING" || recordingStatus == "GENERATED")
      {
        kodi::addon::PVRRecording kodiRecording;
        FillPVRRecording(recordings[i], kodiRecording);
        results.Add(kodiRecording);
        kodi::Log(ADDON_LOG_DEBUG, "Recording added: %s", kodiRecording.GetTitle().c_str());
      }
    }
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta2::DeleteRecording(const kodi::addon::PVRRecording& recording)
{
    kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

    std::string url = m_pvrBaseUrl + "/delete-recording-for-listing/"+recording.GetRecordingId();
    if (SendDeleteRequest(url)) 
    {
      usleep(2000000); //Sometimes magenta needs some time to propagate the info. Wait 2 seconds
      kodi::QueueNotification(QUEUE_INFO, "Aufnahme", "Aufnahme gelöscht");
      m_instancePVRClient->TriggerRecordingUpdate();
      return PVR_ERROR_NO_ERROR;
    }
    else
    {
      return PVR_ERROR_FAILED;
    }
}

PVR_ERROR CPVRMagenta2::GetRecordingStreamProperties(
    const kodi::addon::PVRRecording& recording,
    std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string url = m_pvrBaseUrl + "/get-recordings?limit=500";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return PVR_ERROR_FAILED;
  }

  if (!doc.HasMember("recordings"))
    return PVR_ERROR_FAILED;

  const rapidjson::Value& recordings = doc["recordings"];

  for (rapidjson::SizeType i = 0; i < recordings.Size(); i++)
  {
    const rapidjson::Value& listing = recordings[i]["listing"];
    //kodiRecording.SetRecordingId(Utils::JsonStringOrEmpty(recordingItem, "id"));
    // we need the listing guid because that is needed for deletion of a recording
    if (recording.GetRecordingId() != Utils::JsonStringOrEmpty(listing, "guid"))
      continue;

    std::string playUrl = Utils::JsonStringOrEmpty(recordings[i], "playbackUrl");
    kodi::Log(ADDON_LOG_DEBUG, "[PLAY RECORDING] url: %s", playUrl.c_str());

    SetStreamProperties(properties, playUrl, false, false, false);
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVRMagenta2::GetDriveSpace(uint64_t& total, uint64_t& used)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::string url = m_pvrBaseUrl + "/get-npvr-info";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    return PVR_ERROR_FAILED;
  }

  int quotaAllocated = Utils::JsonIntOrZero(doc, "quotaAllocated");
  int quotaUsed = Utils::JsonIntOrZero(doc, "quotaUsed");

  total = quotaAllocated * KBM2; //convert hours to MB
  used = quotaUsed * KBM2; //convert hours to MB
  kodi::Log(ADDON_LOG_DEBUG, "Reported %llu/%llu used/total", used, total);
  return PVR_ERROR_NO_ERROR;
}

void CPVRMagenta2::OnRefreshEveryMinute()
{
  kodi::Log(ADDON_LOG_DEBUG, "Triggering timer and recordings refresh...");
  m_instancePVRClient->TriggerRecordingUpdate();
  m_instancePVRClient->TriggerTimerUpdate();
}
