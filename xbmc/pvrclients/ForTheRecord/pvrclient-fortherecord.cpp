/*
 *      Copyright (C) 2010 Marcel Groothuis
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "client.h"
//#include "timers.h"
#include "channel.h"
#include "upcomingrecording.h"
#include "recordinggroup.h"
#include "recordingsummary.h"
#include "recording.h"
#include "epg.h"
#include "utils.h"
#include "pvrclient-fortherecord.h"
#include "fortherecordrpc.h"

using namespace std;

/************************************************************/
/** Class interface */

cPVRClientForTheRecord::cPVRClientForTheRecord()
{
  m_bConnected             = false;
  //m_bStop                  = true;
  m_bTimeShiftStarted      = false;
  m_BackendUTCoffset       = 0;
  m_BackendTime            = 0;
  m_tsreader               = NULL;
  m_channel_id_offset      = 0;
  m_epg_id_offset          = 0;
}

cPVRClientForTheRecord::~cPVRClientForTheRecord()
{
  XBMC->Log(LOG_DEBUG, "->~cPVRClientForTheRecord()");
}


bool cPVRClientForTheRecord::Connect()
{
  string result;
  char buffer[256];

  snprintf(buffer, 256, "http://%s:%i/", g_szHostname.c_str(), g_iPort);
  g_szBaseURL = buffer;

  XBMC->Log(LOG_INFO, "Connect() - Connecting to %s", g_szBaseURL.c_str());

  int backendversion = FTR_REST_MAXIMUM_API_VERSION;
  int rc = ForTheRecord::Ping(backendversion);
  if (rc == 1)
  {
    backendversion = FTR_REST_MINIMUM_API_VERSION;
    rc = ForTheRecord::Ping(backendversion);
  }

  m_BackendVersion = backendversion;

  switch (rc)
  {
  case 0:
    XBMC->Log(LOG_INFO, "Ping Ok. The client and server are compatible, API version %d.\n", m_BackendVersion);
    break;
  case -1:
    XBMC->Log(LOG_NOTICE, "Ping Ok. The client is too old for the server.\n");
    return false;
  case 1:
    XBMC->Log(LOG_NOTICE, "Ping Ok. The client is too new for the server.\n");
    return false;
  default:
    XBMC->Log(LOG_ERROR, "Ping failed... No connection to ForTheRecord.\n");
    return false;
  }

  m_bConnected = true;
  return true;
}

void cPVRClientForTheRecord::Disconnect()
{
  string result;

  XBMC->Log(LOG_INFO, "Disconnect");

  if (m_bTimeShiftStarted)
  {
    //TODO: tell ForTheRecord that it should stop streaming
  }

  m_bConnected = false;
}

/************************************************************/
/** General handling */

// Used among others for the server name string in the "Recordings" view
const char* cPVRClientForTheRecord::GetBackendName(void)
{
  XBMC->Log(LOG_DEBUG, "->GetBackendName()");

  if(m_BackendName.length() == 0)
  {
    m_BackendName = "ForTheRecord (";
    m_BackendName += g_szHostname.c_str();
    m_BackendName += ")";
  }

  return m_BackendName.c_str();
}

const char* cPVRClientForTheRecord::GetBackendVersion(void)
{
  // Don't know how to fetch this from ForTheRecord
  return "0.0";
}

const char* cPVRClientForTheRecord::GetConnectionString(void)
{
  XBMC->Log(LOG_DEBUG, "->GetConnectionString()");

  return g_szBaseURL.c_str();
}

PVR_ERROR cPVRClientForTheRecord::GetDriveSpace(long long *iTotal, long long *iUsed)
{
  *iTotal = 0;
  *iUsed = 0;

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR cPVRClientForTheRecord::GetBackendTime(time_t *localTime, int *gmtOffset)
{
  return PVR_ERROR_SERVER_ERROR;
}

/************************************************************/
/** EPG handling */

PVR_ERROR cPVRClientForTheRecord::GetEpg(PVR_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd)
{
  XBMC->Log(LOG_DEBUG, "->RequestEPGForChannel(%i)", channel.iUniqueId);

  cChannel* ftrchannel = FetchChannel(channel.iUniqueId);

  struct tm* convert = localtime(&iStart);
  struct tm tm_start = *convert;
  convert = localtime(&iEnd);
  struct tm tm_end = *convert;

  if(ftrchannel)
  {
    Json::Value response;
    int retval;

    retval = ForTheRecord::GetEPGData(m_BackendVersion, ftrchannel->GuideChannelID(), tm_start, tm_end, response);

    if (retval != E_FAILED)
    {
      XBMC->Log(LOG_DEBUG, "GetEPGData returned %i, response.type == %i, response.size == %i.", retval, response.type(), response.size());
      if( response.type() == Json::arrayValue)
      {
        int size = response.size();
        EPG_TAG broadcast;
        cEpg epg;

        memset(&broadcast, NULL, sizeof(EPG_TAG));

        // parse channel list
        for ( int index =0; index < size; ++index )
        {
          if (epg.Parse(response[index]))
          {
            m_epg_id_offset++;
            broadcast.iUniqueBroadcastId = m_epg_id_offset;
            broadcast.strTitle           = epg.Title();
            broadcast.iChannelNumber     = channel.iChannelNumber;
            broadcast.startTime          = epg.StartTime();
            broadcast.endTime            = epg.EndTime();
            broadcast.strPlotOutline     = epg.Subtitle();
            broadcast.strPlot            = epg.Description();
            broadcast.strIconPath        = "";
            broadcast.iGenreType         = 0;
            broadcast.iGenreSubType      = 0;
            broadcast.firstAired         = 0;
            broadcast.iParentalRating    = 0;
            broadcast.iStarRating        = 0;
            broadcast.bNotify            = false;
            broadcast.iSeriesNumber      = 0;
            broadcast.iEpisodeNumber     = 0;
            broadcast.iEpisodePartNumber = 0;
            broadcast.strEpisodeName     = "";

            PVR->TransferEpgEntry(handle, &broadcast);
          }
          epg.Reset();
        }
      }
    }
    else
    {
      XBMC->Log(LOG_ERROR, "GetEPGData failed for channel id:%i", channel.iChannelNumber);
    }
  }
  else
  {
    XBMC->Log(LOG_ERROR, "Channel (%i) did not return a channel class.", channel.iChannelNumber);
  }

  return PVR_ERROR_NO_ERROR;
}

bool cPVRClientForTheRecord::FetchGuideProgramDetails(std::string Id, cGuideProgram& guideprogram)
{ 
  bool fRc = false;
  Json::Value guideprogramresponse;

  int retval = ForTheRecord::GetProgramById(Id, guideprogramresponse);
  if (retval >= 0)
  {
    fRc = guideprogram.Parse(guideprogramresponse);
  }
  return fRc;
}

/************************************************************/
/** Channel handling */

int cPVRClientForTheRecord::GetNumChannels()
{
  // Not directly possible in ForTheRecord
  Json::Value response;

  XBMC->Log(LOG_DEBUG, "GetNumChannels()");

  // pick up the channellist for TV
  int retval = ForTheRecord::GetChannelList(ForTheRecord::Television, response);
  if (retval < 0) 
  {
    return 0;
  }

  int numberofchannels = response.size();

  // When radio is enabled, add the number of radio channels
  if (g_bRadioEnabled)
  {
    retval = ForTheRecord::GetChannelList(ForTheRecord::Radio, response);
    if (retval >= 0)
    {
      numberofchannels += response.size();
    }
  }

  return numberofchannels;
}

PVR_ERROR cPVRClientForTheRecord::GetChannels(PVR_HANDLE handle, bool bRadio)
{
  Json::Value response;
  int retval = -1;

  XBMC->Log(LOG_DEBUG, "%s(%s)", __FUNCTION__, bRadio ? "radio" : "television");
  if (!bRadio)
  {
    retval = ForTheRecord::GetChannelList(ForTheRecord::Television, response);
  }
  else
  {
    retval = ForTheRecord::GetChannelList(ForTheRecord::Radio, response);
  }

  if(retval >= 0)
  {           
    int size = response.size();

    // parse channel list
    for ( int index = 0; index < size; ++index )
    {

      cChannel channel;
      if( channel.Parse(response[index]) )
      {
        PVR_CHANNEL tag;
        memset(&tag, 0 , sizeof(tag));
        //Hack: assumes that the order of the channel list is fixed.
        //      We can't use the ForTheRecord channel id's. They are GUID strings (128 bit int).       
        //      But only if it isn't cached yet!
        if (FetchChannel(channel.Guid()) == NULL)
        {
          tag.iChannelNumber =  m_channel_id_offset + 1;
          m_channel_id_offset++;
        }
        else
        {
          tag.iChannelNumber = FetchChannel(channel.Guid())->ID();
        }
        tag.iUniqueId = tag.iChannelNumber;
        tag.strChannelName = channel.Name();
        tag.strIconPath = "";
        tag.iEncryptionSystem = 0; //How to fetch this from ForTheRecord??
        tag.bIsRadio = (channel.Type() == ForTheRecord::Radio ? true : false);
        tag.bIsHidden = false;
        //Use OpenLiveStream to read from the timeshift .ts file or an rtsp stream
        tag.strStreamURL = "";
        tag.strInputFormat = "mpegts";

        if (!tag.bIsRadio)
        {
          XBMC->Log(LOG_DEBUG, "Found TV channel: %s\n", channel.Name());
        }
        else
        {
          XBMC->Log(LOG_DEBUG, "Found Radio channel: %s\n", channel.Name());
        }
        channel.SetID(tag.iUniqueId);
        if (FetchChannel(channel.Guid()) == NULL)
        {
          m_Channels.push_back(channel); //Local cache...
        }
        PVR->TransferChannelEntry(handle, &tag);
      }
    }

    return PVR_ERROR_NO_ERROR;
  }
  else
  {
    XBMC->Log(LOG_DEBUG, "RequestChannelList failed. Return value: %i\n", retval);
  }

  return PVR_ERROR_SERVER_ERROR;
}

/************************************************************/
/** Channel group handling **/

int cPVRClientForTheRecord::GetChannelGroupsAmount(void)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR cPVRClientForTheRecord::GetChannelGroups(PVR_HANDLE handle, bool bRadio)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR cPVRClientForTheRecord::GetChannelGroupMembers(PVR_HANDLE handle, const PVR_CHANNEL_GROUP &group)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

/************************************************************/
/** Record handling **/

int cPVRClientForTheRecord::GetNumRecordings(void)
{
  Json::Value response;
  int retval = -1;
  int iNumRecordings = 0;

  XBMC->Log(LOG_DEBUG, "GetNumRecordings()");
  retval = ForTheRecord::GetRecordingGroupByTitle(response);
  if (retval >= 0)
  {
    int size = response.size();

    // parse channelgroup list
    for ( int index = 0; index < size; ++index )
    {
      cRecordingGroup recordinggroup;
      if (recordinggroup.Parse(response[index]))
      {
        iNumRecordings += recordinggroup.RecordingsCount();
      }
    }
  }
  return iNumRecordings;
}

PVR_ERROR cPVRClientForTheRecord::GetRecordings(PVR_HANDLE handle)
{
  Json::Value recordinggroupresponse;
  int retval = -1;
  int iNumRecordings = 0;

  XBMC->Log(LOG_DEBUG, "RequestRecordingsList()");
  retval = ForTheRecord::GetRecordingGroupByTitle(recordinggroupresponse);
  if(retval >= 0)
  {           
    // process list of recording groups
    int size = recordinggroupresponse.size();
    for ( int recordinggroupindex = 0; recordinggroupindex < size; ++recordinggroupindex )
    {
      cRecordingGroup recordinggroup;
      if (recordinggroup.Parse(recordinggroupresponse[recordinggroupindex]))
      {
        Json::Value recordingsbytitleresponse;
        retval = ForTheRecord::GetRecordingsForTitle(recordinggroup.ProgramTitle(), recordingsbytitleresponse);
        if (retval >= 0)
        {
          // process list of recording summaries for this group
          int nrOfRecordings = recordingsbytitleresponse.size();
          for (int recordingindex = 0; recordingindex < nrOfRecordings; recordingindex++)
          {
            cRecording recording;
            if (FetchRecordingDetails(recordingsbytitleresponse[recordingindex], recording))
            {
              PVR_RECORDING tag;
              memset(&tag, 0 , sizeof(tag));
              tag.iClientIndex   = iNumRecordings;
              tag.strChannelName = recording.ChannelDisplayName();
              tag.iLifetime      = MAXLIFETIME; //TODO: recording.Lifetime();
              tag.iPriority      = 0; //TODO? recording.Priority();
              tag.recordingTime  = recording.RecordingStartTime();
              tag.iDuration      = recording.RecordingStopTime() - recording.RecordingStartTime();
              tag.strPlot        = recording.Description();;
              tag.strTitle       = recording.Title();
              tag.strPlotOutline = recording.SubTitle();
              if (nrOfRecordings > 1)
              {
                tag.strDirectory = recordinggroup.ProgramTitle().c_str(); //used in XBMC as directory structure below "Server X - hostname"
              }
              else
              {
                tag.strDirectory = "";
              }
              tag.strStreamURL   = recording.RecordingFileName();
              PVR->TransferRecordingEntry(handle, &tag);
              iNumRecordings++;
            }
          }
        }
      }
    }
  }
  return PVR_ERROR_NO_ERROR;
}

bool cPVRClientForTheRecord::FetchRecordingDetails(const Json::Value& data, cRecording& recording)
{ 
  bool fRc = false;
  Json::Value recordingresponse;

  cRecordingSummary recordingsummary;
  if (recordingsummary.Parse(data))
  {
    int retval = ForTheRecord::GetRecordingById(recordingsummary.RecordingId(), recordingresponse);
    if (retval >= 0)
    {
      if (recordingresponse.type() == Json::objectValue)
      {
        fRc = recording.Parse(recordingresponse);
      }
    }
  }
  return fRc;
}

PVR_ERROR cPVRClientForTheRecord::DeleteRecording(const PVR_RECORDING &recinfo)
{
  // JSONify the stream_url
  Json::Value recordingname (recinfo.strStreamURL);
  Json::StyledWriter writer;
  std::string jsonval = writer.write(recordingname);
  if (ForTheRecord::DeleteRecording(jsonval) >= 0) 
  {
    return PVR_ERROR_NO_ERROR;
  }
  else
  {
    return PVR_ERROR_NOT_DELETED;
  }
}

PVR_ERROR cPVRClientForTheRecord::RenameRecording(const PVR_RECORDING &recinfo)
{
  return PVR_ERROR_NO_ERROR;
}


/************************************************************/
/** Timer handling */

int cPVRClientForTheRecord::GetNumTimers(void)
{
  // Not directly possible in ForTheRecord
  Json::Value response;

  XBMC->Log(LOG_DEBUG, "GetNumTimers()");
  // pick up the schedulelist for TV
  int retval = ForTheRecord::GetUpcomingPrograms(response);
  if (retval < 0) 
  {
    return 0;
  }

  return response.size();
}

PVR_ERROR cPVRClientForTheRecord::GetTimers(PVR_HANDLE handle)
{
  Json::Value response;
  int         iNumberOfTimers = 0;
  PVR_TIMER   tag;
  int         numberoftimers;

  XBMC->Log(LOG_DEBUG, "%s", __FUNCTION__);

  // pick up the upcoming recordings
  int retval = ForTheRecord::GetUpcomingPrograms(response);
  if (retval < 0) 
  {
    return PVR_ERROR_SERVER_ERROR;
  }

  memset(&tag, 0 , sizeof(tag));
  numberoftimers = response.size();

  for (int i = 0; i < numberoftimers; i++)
  {
    cUpcomingRecording upcomingrecording;
    if (upcomingrecording.Parse(response[i]))
    {
      tag.iClientIndex      = iNumberOfTimers;
      tag.bIsActive         = true;
      cChannel* pChannel    = FetchChannel(upcomingrecording.ChannelId());
      tag.iClientChannelUid = pChannel->ID();
      tag.firstDay          = 0;
      tag.iMarginStart      = upcomingrecording.PreRecordSeconds() / 60;
      tag.iMarginEnd        = upcomingrecording.PostRecordSeconds() / 60;
      tag.startTime         = upcomingrecording.StartTime();
      tag.endTime           = upcomingrecording.StopTime();
      tag.bIsRecording      = upcomingrecording.IsRecording();
      tag.strTitle          = upcomingrecording.Title().c_str();
      tag.strDirectory      = "";
      tag.iPriority         = 0;
      tag.iLifetime         = 0;
      tag.bIsRepeating      = false;
      tag.iWeekdays         = 0;

      PVR->TransferTimerEntry(handle, &tag);
      iNumberOfTimers++;
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR cPVRClientForTheRecord::GetTimerInfo(unsigned int timernumber, PVR_TIMER &tag)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR cPVRClientForTheRecord::AddTimer(const PVR_TIMER &timerinfo)
{
  XBMC->Log(LOG_DEBUG, "AddTimer()");

  // re-synthesize the FTR startime, stoptime and channel GUID
  time_t starttime = timerinfo.startTime;
  cChannel* pChannel = FetchChannel(timerinfo.iClientChannelUid);

  int retval = ForTheRecord::AddOneTimeSchedule(pChannel->Guid(), starttime, timerinfo.strTitle, timerinfo.iMarginStart * 60, timerinfo.iMarginEnd * 60);
  if (retval < 0) 
  {
    return PVR_ERROR_SERVER_ERROR;
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR cPVRClientForTheRecord::DeleteTimer(const PVR_TIMER &timerinfo, bool force)
{
  Json::Value response;

  XBMC->Log(LOG_DEBUG, "DeleteTimer()");

  // re-synthesize the FTR startime, stoptime and channel GUID
  time_t starttime = timerinfo.startTime;
  time_t stoptime = timerinfo.endTime;
  cChannel* pChannel = FetchChannel(timerinfo.iClientChannelUid);

  // pick up the upcoming recordings
  int retval = ForTheRecord::GetUpcomingPrograms(response);
  if (retval < 0) 
  {
    return PVR_ERROR_SERVER_ERROR;
  }

  // try to find the upcoming recording that matches this xbmc timer
  int numberoftimers = response.size();
  for (int i = 0; i < numberoftimers; i++)
  {
    cUpcomingRecording upcomingrecording;
    if (upcomingrecording.Parse(response[i]))
    {
      if (upcomingrecording.ChannelId() == pChannel->Guid())
      {
        if (upcomingrecording.StartTime() == starttime)
        {
          if (upcomingrecording.StopTime() == stoptime)
          {
            retval = ForTheRecord::CancelUpcomingProgram(upcomingrecording.ScheduleId(), upcomingrecording.ChannelId(),
              upcomingrecording.StartTime(), upcomingrecording.UpcomingProgramId());
            if (retval >= 0) return PVR_ERROR_NO_ERROR;
            else return PVR_ERROR_SERVER_ERROR;
          }
        }
      }
    }
  }
  return PVR_ERROR_NOT_POSSIBLE;
}

PVR_ERROR cPVRClientForTheRecord::UpdateTimer(const PVR_TIMER &timerinfo)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}


/************************************************************/
/** Live stream handling */
cChannel* cPVRClientForTheRecord::FetchChannel(int channel_uid)
{
  // Search for this channel in our local channel list to find the original ChannelID back:
  vector<cChannel>::iterator it;

  for ( it=m_Channels.begin(); it < m_Channels.end(); it++ )
  {
    if (it->ID() == channel_uid)
    {
      return &*it;
      break;
    }
  }

  return NULL;
}

cChannel* cPVRClientForTheRecord::FetchChannel(std::string channelid)
{
  // Search for this channel in our local channel list to find the original ChannelID back:
  vector<cChannel>::iterator it;

  for ( it=m_Channels.begin(); it < m_Channels.end(); it++ )
  {
    if (it->Guid() == channelid)
    {
      return &*it;
      break;
    }
  }

  return NULL;
}


bool cPVRClientForTheRecord::OpenLiveStream(const PVR_CHANNEL &channelinfo)
{
  XBMC->Log(LOG_DEBUG, "->OpenLiveStream(%i)", channelinfo.iChannelNumber);

  cChannel* channel = FetchChannel(channelinfo.iUniqueId);

  if (channel)
  {
    std::string filename;
    XBMC->Log(LOG_INFO, "Tune XBMC channel: %i", channelinfo.iChannelNumber);
    XBMC->Log(LOG_INFO, "Corresponding ForTheRecord channel: %s", channel->Guid().c_str());

    ForTheRecord::TuneLiveStream(channel->Guid(), channel->Type(), filename);

    if (filename.length() == 0)
    {
      XBMC->Log(LOG_ERROR, "Could not start the timeshift for channel %i (%s)", channelinfo.iChannelNumber, channel->Guid().c_str());
      return false;
    }

    XBMC->Log(LOG_INFO, "Live stream file: %s", filename.c_str());
    m_bTimeShiftStarted = true;

#ifdef TSREADER
    if (m_tsreader != NULL)
    {
      m_keepalive.StopThread(0);
      m_tsreader->Close();
      delete m_tsreader;
      m_tsreader = new CTsReader();
    } else {
      m_tsreader = new CTsReader();
    }

    // Open Timeshift buffer
    // TODO: rtsp support
    m_tsreader->Open(filename.c_str());
    m_keepalive.StartThread();
#endif
    return true;
  }

  return false;
}

int cPVRClientForTheRecord::ReadLiveStream(unsigned char* pBuffer, unsigned int iBufferSize)
{
#ifdef TSREADER
  unsigned long read_wanted = iBufferSize;
  unsigned long read_done   = 0;
  static int read_timeouts  = 0;
  unsigned char* bufptr = pBuffer;

  //XBMC->Log(LOG_DEBUG, "->ReadLiveStream(buf_size=%i)", buf_size);

  while (read_done < (unsigned long) iBufferSize)
  {
    read_wanted = iBufferSize - read_done;
    if (!m_tsreader)
      return -1;

    if (m_tsreader->Read(bufptr, read_wanted, &read_wanted) > 0)
    {
      usleep(20000);
      read_timeouts++;
      return read_wanted;
    }
    read_done += read_wanted;

    if ( read_done < (unsigned long) iBufferSize )
    {
      if (read_timeouts > 50)
      {
        XBMC->Log(LOG_INFO, "No data in 1 second");
        read_timeouts = 0;
        return read_done;
      }
      bufptr += read_wanted;
      read_timeouts++;
      usleep(20000);
    }
  }
  read_timeouts = 0;
  return read_done;
#else
  return 0;
#endif //TSREADER
}

void cPVRClientForTheRecord::CloseLiveStream()
{
  string result;

  if (m_bTimeShiftStarted)
  {
#ifdef TSREADER
    if (m_tsreader)
    {
      m_tsreader->Close();
      delete_null(m_tsreader);
    }
    m_keepalive.StopThread();
#endif
    ForTheRecord::StopLiveStream();
    XBMC->Log(LOG_INFO, "CloseLiveStream");
    m_bTimeShiftStarted = false;
  } else {
    XBMC->Log(LOG_DEBUG, "CloseLiveStream: Nothing to do.");
  }
}


bool cPVRClientForTheRecord::SwitchChannel(const PVR_CHANNEL &channelinfo)
{
  return OpenLiveStream(channelinfo);
}


int cPVRClientForTheRecord::GetCurrentClientChannel()
{
  return 0;
}

PVR_ERROR cPVRClientForTheRecord::SignalStatus(PVR_SIGNAL_STATUS &signalStatus)
{
  return PVR_ERROR_NO_ERROR;
}


/************************************************************/
/** Record stream handling */
bool cPVRClientForTheRecord::OpenRecordedStream(const PVR_RECORDING &recinfo)
{
  XBMC->Log(LOG_DEBUG, "->OpenRecordedStream(index=%i)", recinfo.iClientIndex);

  return false;
}


void cPVRClientForTheRecord::CloseRecordedStream(void)
{
}

int cPVRClientForTheRecord::ReadRecordedStream(unsigned char* pBuffer, unsigned int iBuffersize)
{
  return -1;
}

/*
 * \brief Request the stream URL for live tv/live radio.
 */
const char* cPVRClientForTheRecord::GetLiveStreamURL(const PVR_CHANNEL &channelinfo)
{
  return false;
}
