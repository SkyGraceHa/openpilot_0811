#include "selfdrive/common/params.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif  // _GNU_SOURCE

#include <dirent.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "selfdrive/common/swaglog.h"
#include "selfdrive/common/util.h"
#include "selfdrive/hardware/hw.h"

namespace {

volatile sig_atomic_t params_do_exit = 0;
void params_sig_handler(int signal) {
  params_do_exit = 1;
}

int fsync_dir(const char* path) {
  int fd = HANDLE_EINTR(open(path, O_RDONLY, 0755));
  if (fd < 0) {
    return -1;
  }

  int result = fsync(fd);
  int result_close = close(fd);
  if (result_close < 0) {
    result = result_close;
  }
  return result;
}


bool create_params_path(const std::string &param_path, const std::string &key_path) {
  // Make sure params path exists
  if (!util::file_exists(param_path) && !util::create_directories(param_path, 0775)) {
    return false;
  }

  // See if the symlink exists, otherwise create it
  if (!util::file_exists(key_path)) {
    // 1) Create temp folder
    // 2) Set permissions
    // 3) Symlink it to temp link
    // 4) Move symlink to <params>/d

    std::string tmp_path = param_path + "/.tmp_XXXXXX";
    // this should be OK since mkdtemp just replaces characters in place
    char *tmp_dir = mkdtemp((char *)tmp_path.c_str());
    if (tmp_dir == NULL) {
      return false;
    }

    std::string link_path = std::string(tmp_dir) + ".link";
    if (symlink(tmp_dir, link_path.c_str()) != 0) {
      return false;
    }

    // don't return false if it has been created by other
    if (rename(link_path.c_str(), key_path.c_str()) != 0 && errno != EEXIST) {
      return false;
    }
  }

  return true;
}

void ensure_params_path(const std::string &params_path) {
  if (!create_params_path(params_path, params_path + "/d")) {
    throw std::runtime_error(util::string_format("Failed to ensure params path, errno=%d", errno));
  }
}

class FileLock {
 public:
  FileLock(const std::string& file_name, int op) : fn_(file_name), op_(op) {}

  void lock() {
    fd_ = HANDLE_EINTR(open(fn_.c_str(), O_CREAT, 0775));
    if (fd_ < 0) {
      LOGE("Failed to open lock file %s, errno=%d", fn_.c_str(), errno);
      return;
    }
    if (HANDLE_EINTR(flock(fd_, op_)) < 0) {
      LOGE("Failed to lock file %s, errno=%d", fn_.c_str(), errno);
    }
  }

  void unlock() { close(fd_); }

private:
  int fd_ = -1, op_;
  std::string fn_;
};

std::unordered_map<std::string, uint32_t> keys = {
    {"AccessToken", CLEAR_ON_MANAGER_START | DONT_LOG},
    {"AthenadPid", PERSISTENT},
    {"BootedOnroad", CLEAR_ON_MANAGER_START | CLEAR_ON_IGNITION_OFF},
    {"CalibrationParams", PERSISTENT},
    {"CarBatteryCapacity", PERSISTENT},
    {"CarParams", CLEAR_ON_MANAGER_START | CLEAR_ON_PANDA_DISCONNECT | CLEAR_ON_IGNITION_ON},
    {"CarParamsCache", CLEAR_ON_MANAGER_START | CLEAR_ON_PANDA_DISCONNECT},
    {"CarVin", CLEAR_ON_MANAGER_START | CLEAR_ON_PANDA_DISCONNECT | CLEAR_ON_IGNITION_ON},
    {"CommunityFeaturesToggle", PERSISTENT},
    {"CompletedTrainingVersion", PERSISTENT},
    {"ControlsReady", CLEAR_ON_MANAGER_START | CLEAR_ON_PANDA_DISCONNECT | CLEAR_ON_IGNITION_ON},
    {"CurrentRoute", CLEAR_ON_MANAGER_START | CLEAR_ON_IGNITION_ON},
    {"DisablePowerDown", PERSISTENT},
    {"DisableRadar_Allow", PERSISTENT},
    {"DisableRadar", PERSISTENT}, // WARNING: THIS DISABLES AEB
    {"DisableUpdates", PERSISTENT},
    {"DongleId", PERSISTENT},
    {"DoUninstall", CLEAR_ON_MANAGER_START},
    {"EnableWideCamera", CLEAR_ON_MANAGER_START},
    {"EndToEndToggle", PERSISTENT},
    {"ForcePowerDown", CLEAR_ON_MANAGER_START},
    {"GitBranch", PERSISTENT},
    {"GitCommit", PERSISTENT},
    {"GitDiff", PERSISTENT},
    {"GithubSshKeys", PERSISTENT},
    {"GithubUsername", PERSISTENT},
    {"GitRemote", PERSISTENT},
    {"GsmApn", PERSISTENT},
    {"GsmRoaming", PERSISTENT},
    {"HardwareSerial", PERSISTENT},
    {"HasAcceptedTerms", PERSISTENT},
    {"IMEI", PERSISTENT},
    {"InstallDate", PERSISTENT},
    {"IsDriverViewEnabled", CLEAR_ON_MANAGER_START},
    {"IsLdwEnabled", PERSISTENT},
    {"IsMetric", PERSISTENT},
    {"IsOffroad", CLEAR_ON_MANAGER_START},
    {"IsOnroad", PERSISTENT},
    {"IsRHD", PERSISTENT},
    {"IsTakingSnapshot", CLEAR_ON_MANAGER_START},
    {"IsUpdateAvailable", CLEAR_ON_MANAGER_START},
    {"JoystickDebugMode", CLEAR_ON_MANAGER_START | CLEAR_ON_IGNITION_OFF},
    {"LastAthenaPingTime", CLEAR_ON_MANAGER_START},
    {"LastGPSPosition", PERSISTENT},
    {"LastUpdateException", PERSISTENT},
    {"LastUpdateTime", PERSISTENT},
    {"LiveParameters", PERSISTENT},
    {"NavDestination", CLEAR_ON_MANAGER_START | CLEAR_ON_IGNITION_OFF},
    {"NavSettingTime24h", PERSISTENT},
    {"OpenpilotEnabledToggle", PERSISTENT},
    {"PandaHeartbeatLost", CLEAR_ON_MANAGER_START | CLEAR_ON_IGNITION_OFF},
    {"Passive", PERSISTENT},
    {"PrimeRedirected", PERSISTENT},
    {"RecordFront", PERSISTENT},
    {"RecordFrontLock", PERSISTENT},  // for the internal fleet
    {"ReleaseNotes", PERSISTENT},
    {"ShouldDoUpdate", CLEAR_ON_MANAGER_START},
    {"SshEnabled", PERSISTENT},
    {"SubscriberInfo", PERSISTENT},
    {"TermsVersion", PERSISTENT},
    {"Timezone", PERSISTENT},
    {"TrainingVersion", PERSISTENT},
    {"UpdateAvailable", CLEAR_ON_MANAGER_START},
    {"UpdateFailedCount", CLEAR_ON_MANAGER_START},
    {"UploadRaw", PERSISTENT},
    {"Version", PERSISTENT},
    {"VisionRadarToggle", PERSISTENT},
    {"ApiCache_Device", PERSISTENT},
    {"ApiCache_DriveStats", PERSISTENT},
    {"ApiCache_NavDestinations", PERSISTENT},
    {"ApiCache_Owner", PERSISTENT},
    {"Offroad_ChargeDisabled", CLEAR_ON_MANAGER_START | CLEAR_ON_PANDA_DISCONNECT},
    {"Offroad_ConnectivityNeeded", CLEAR_ON_MANAGER_START},
    {"Offroad_ConnectivityNeededPrompt", CLEAR_ON_MANAGER_START},
    {"Offroad_HardwareUnsupported", CLEAR_ON_MANAGER_START},
    {"Offroad_InvalidTime", CLEAR_ON_MANAGER_START},
    {"Offroad_IsTakingSnapshot", CLEAR_ON_MANAGER_START},
    {"Offroad_NeosUpdate", CLEAR_ON_MANAGER_START},
    {"Offroad_StorageMissing", CLEAR_ON_MANAGER_START},
    {"Offroad_PandaFirmwareMismatch", CLEAR_ON_MANAGER_START | CLEAR_ON_PANDA_DISCONNECT},
    {"Offroad_TemperatureTooHigh", CLEAR_ON_MANAGER_START},
    {"Offroad_UnofficialHardware", CLEAR_ON_MANAGER_START},
    {"Offroad_UpdateFailed", CLEAR_ON_MANAGER_START},
    {"GitCommitRemote", PERSISTENT},
    {"IsOpenpilotViewEnabled", CLEAR_ON_MANAGER_START},
    {"OpkrAutoShutdown", PERSISTENT},
    {"OpkrForceShutdown", PERSISTENT},
    {"OpkrForceShutdownTrigger", PERSISTENT},
    {"OpkrAutoScreenOff", PERSISTENT},
    {"OpkrUIBrightness", PERSISTENT},
    {"OpkrUIVolumeBoost", PERSISTENT},
    {"OpkrEnableDriverMonitoring", PERSISTENT},
    {"OpkrEnableLogger", PERSISTENT},
    {"OpkrEnableGetoffAlert", PERSISTENT},
    {"OpkrAutoResume", PERSISTENT},
    {"OpkrVariableCruise", PERSISTENT},
    {"OpkrLaneChangeSpeed", PERSISTENT},
    {"OpkrAutoLaneChangeDelay", PERSISTENT},
    {"OpkrSteerAngleCorrection", PERSISTENT},
    {"PutPrebuiltOn", PERSISTENT},
    {"LdwsCarFix", PERSISTENT},
    {"LateralControlMethod", PERSISTENT},
    {"CruiseStatemodeSelInit", PERSISTENT},
    {"OuterLoopGain", PERSISTENT},
    {"InnerLoopGain", PERSISTENT},
    {"TimeConstant", PERSISTENT},
    {"ActuatorEffectiveness", PERSISTENT},
    {"Scale", PERSISTENT},
    {"LqrKi", PERSISTENT},
    {"DcGain", PERSISTENT},
    {"IgnoreZone", PERSISTENT},
    {"PidKp", PERSISTENT},
    {"PidKi", PERSISTENT},
    {"PidKd", PERSISTENT},
    {"PidKf", PERSISTENT},
    {"CameraOffsetAdj", PERSISTENT},
    {"PathOffsetAdj", PERSISTENT},
    {"SteerRatioAdj", PERSISTENT},
    {"SteerRatioMaxAdj", PERSISTENT},
    {"SteerActuatorDelayAdj", PERSISTENT},
    {"SteerRateCostAdj", PERSISTENT},
    {"SteerLimitTimerAdj", PERSISTENT},
    {"TireStiffnessFactorAdj", PERSISTENT},
    {"SteerMaxAdj", PERSISTENT},
    {"SteerMaxBaseAdj", PERSISTENT},
    {"SteerDeltaUpAdj", PERSISTENT},
    {"SteerDeltaUpBaseAdj", PERSISTENT},
    {"SteerDeltaDownAdj", PERSISTENT},
    {"SteerDeltaDownBaseAdj", PERSISTENT},
    {"SteerMaxvAdj", PERSISTENT},
    {"OpkrBatteryChargingControl", PERSISTENT},
    {"OpkrBatteryChargingMin", PERSISTENT},
    {"OpkrBatteryChargingMax", PERSISTENT},
    {"LeftCurvOffsetAdj", PERSISTENT},
    {"RightCurvOffsetAdj", PERSISTENT},
    {"DebugUi1", PERSISTENT},
    {"DebugUi2", PERSISTENT},
    {"LongLogDisplay", PERSISTENT},
    {"OpkrBlindSpotDetect", PERSISTENT},
    {"OpkrMaxAngleLimit", PERSISTENT},
    {"OpkrSpeedLimitOffset", PERSISTENT},
    {"OpkrLiveSteerRatio", PERSISTENT},
    {"OpkrVariableSteerMax", PERSISTENT},
    {"OpkrVariableSteerDelta", PERSISTENT},
    {"FingerprintTwoSet", PERSISTENT},
    {"OpkrDrivingRecord", PERSISTENT},
    {"OpkrTurnSteeringDisable", PERSISTENT},
    {"CarModel", PERSISTENT},
    {"OpkrHotspotOnBoot", PERSISTENT},
    {"OpkrSSHLegacy", PERSISTENT},
    {"CruiseOverMaxSpeed", PERSISTENT},
    {"JustDoGearD", PERSISTENT},
    {"LanelessMode", PERSISTENT},
    {"ComIssueGone", PERSISTENT},
    {"MaxSteer", PERSISTENT},
    {"MaxRTDelta", PERSISTENT},
    {"MaxRateUp", PERSISTENT},
    {"MaxRateDown", PERSISTENT},
    {"SteerThreshold", PERSISTENT},
    {"RecordingCount", PERSISTENT},
    {"RecordingQuality", PERSISTENT},
    {"CruiseGapAdjust", PERSISTENT},
    {"AutoEnable", PERSISTENT},
    {"AutoEnableSpeed", PERSISTENT},
    {"CruiseAutoRes", PERSISTENT},
    {"AutoResOption", PERSISTENT},
    {"AutoResCondition", PERSISTENT},
    {"SteerWindDown", PERSISTENT},
    {"OpkrMonitoringMode", PERSISTENT},
    {"OpkrMonitorEyesThreshold", PERSISTENT},
    {"OpkrMonitorNormalEyesThreshold", PERSISTENT},
    {"OpkrMonitorBlinkThreshold", PERSISTENT},
    {"MadModeEnabled", PERSISTENT},
    {"CommaStockUI", PERSISTENT},
    {"OpkrEnableUploader", PERSISTENT},
    {"OpkrMapEnable", CLEAR_ON_MANAGER_START},
    {"WhitePandaSupport", PERSISTENT},
    {"SteerWarningFix", PERSISTENT},
    {"OpkrRunNaviOnBoot", PERSISTENT},
    {"CruiseGapNow", PERSISTENT},
    {"CruiseGap1", PERSISTENT},
    {"CruiseGap2", PERSISTENT},
    {"CruiseGap3", PERSISTENT},
    {"CruiseGap4", PERSISTENT},
    {"DynamicTR", PERSISTENT},
    {"OpkrBattLess", PERSISTENT},
    {"LCTimingFactorUD", PERSISTENT},
    {"LCTimingFactor30", PERSISTENT},
    {"LCTimingFactor60", PERSISTENT},
    {"LCTimingFactor80", PERSISTENT},
    {"LCTimingFactor110", PERSISTENT},
    {"OpkrUIBrightnessOff", PERSISTENT},
    {"LCTimingFactorEnable", PERSISTENT},
    {"SafetyCamDecelDistGain", PERSISTENT},
    {"OpkrLiveTunePanelEnable", PERSISTENT},
    {"KRDateShow", PERSISTENT},
    {"KRTimeShow", PERSISTENT},
    {"RadarLongHelper", PERSISTENT},
    {"GitPullOnBoot", PERSISTENT},
    {"LiveSteerRatioPercent", PERSISTENT},
    {"StoppingDistAdj", PERSISTENT},
    {"ShowError", PERSISTENT},
    {"CommaLong", PERSISTENT},
    {"AutoResLimitTime", PERSISTENT},
    {"VCurvSpeed30", PERSISTENT},
    {"VCurvSpeed50", PERSISTENT},
    {"VCurvSpeed70", PERSISTENT},
    {"VCurvSpeed90", PERSISTENT},
    {"VCurvSpeedUD", PERSISTENT},
    {"OCurvOffset", PERSISTENT},
    {"StockNaviSpeedEnabled", PERSISTENT},
};

} // namespace

Params::Params() : params_path(Path::params()) {
  static std::once_flag once_flag;
  std::call_once(once_flag, ensure_params_path, params_path);
}

Params::Params(const std::string &path) : params_path(path) {
  ensure_params_path(params_path);
}

bool Params::checkKey(const std::string &key) {
  return keys.find(key) != keys.end();
}

ParamKeyType Params::getKeyType(const std::string &key) {
  return static_cast<ParamKeyType>(keys[key]);
}

int Params::put(const char* key, const char* value, size_t value_size) {
  // Information about safely and atomically writing a file: https://lwn.net/Articles/457667/
  // 1) Create temp file
  // 2) Write data to temp file
  // 3) fsync() the temp file
  // 4) rename the temp file to the real name
  // 5) fsync() the containing directory
  std::string tmp_path = params_path + "/.tmp_value_XXXXXX";
  int tmp_fd = mkstemp((char*)tmp_path.c_str());
  if (tmp_fd < 0) return -1;

  int result = -1;
  do {
    // Write value to temp.
    ssize_t bytes_written = HANDLE_EINTR(write(tmp_fd, value, value_size));
    if (bytes_written < 0 || (size_t)bytes_written != value_size) {
      result = -20;
      break;
    }

    // fsync to force persist the changes.
    if ((result = fsync(tmp_fd)) < 0) break;

    FileLock file_lock(params_path + "/.lock", LOCK_EX);
    std::lock_guard<FileLock> lk(file_lock);

    // Move temp into place.
    std::string path = params_path + "/d/" + std::string(key);
    if ((result = rename(tmp_path.c_str(), path.c_str())) < 0) break;

    // fsync parent directory
    path = params_path + "/d";
    result = fsync_dir(path.c_str());
  } while (false);

  close(tmp_fd);
  ::unlink(tmp_path.c_str());
  return result;
}

int Params::remove(const char *key) {
  FileLock file_lock(params_path + "/.lock", LOCK_EX);
  std::lock_guard<FileLock> lk(file_lock);
  // Delete value.
  std::string path = params_path + "/d/" + key;
  int result = unlink(path.c_str());
  if (result != 0) {
    return result;
  }
  // fsync parent directory
  path = params_path + "/d";
  return fsync_dir(path.c_str());
}

std::string Params::get(const char *key, bool block) {
  std::string path = params_path + "/d/" + key;
  if (!block) {
    return util::read_file(path);
  } else {
    // blocking read until successful
    params_do_exit = 0;
    void (*prev_handler_sigint)(int) = std::signal(SIGINT, params_sig_handler);
    void (*prev_handler_sigterm)(int) = std::signal(SIGTERM, params_sig_handler);

    std::string value;
    while (!params_do_exit) {
      if (value = util::read_file(path); !value.empty()) {
        break;
      }
      util::sleep_for(100);  // 0.1 s
    }

    std::signal(SIGINT, prev_handler_sigint);
    std::signal(SIGTERM, prev_handler_sigterm);
    return value;
  }
}

std::map<std::string, std::string> Params::readAll() {
  FileLock file_lock(params_path + "/.lock", LOCK_SH);
  std::lock_guard<FileLock> lk(file_lock);

  std::string key_path = params_path + "/d";
  return util::read_files_in_dir(key_path);
}

void Params::clearAll(ParamKeyType key_type) {
  FileLock file_lock(params_path + "/.lock", LOCK_EX);
  std::lock_guard<FileLock> lk(file_lock);

  std::string path;
  for (auto &[key, type] : keys) {
    if (type & key_type) {
      path = params_path + "/d/" + key;
      unlink(path.c_str());
    }
  }

  // fsync parent directory
  path = params_path + "/d";
  fsync_dir(path.c_str());
}
