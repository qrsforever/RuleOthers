/***************************************************************************
 *  DeviceDataChannel.cpp - Device Data Channel Impl
 *
 *  Created: 2018-06-14 15:05:46
 *
 *  Copyright QRS
 ****************************************************************************/

#include "DeviceDataChannel.h"
#include "RuleEventHandler.h"
#include "RuleEventTypes.h"
#include "InstancePayload.h"
#include "Log.h"

#include "TempSimulateSuite.h" /* TODO only test */

using namespace UTILS;

namespace HB {

DeviceDataChannel::DeviceDataChannel()
    : mDeviceMgr(deviceManager())
    , mH(ruleHandler())
{

}

DeviceDataChannel::~DeviceDataChannel()
{

}

int DeviceDataChannel::init()
{
    LOGTT();

    /* regist device state changed callback */
    mDeviceMgr.registDeviceStateChangedCallback(
        std::bind(
            &DeviceDataChannel::onStateChanged,
            this,
            std::placeholders::_1,
            std::placeholders::_2,
            std::placeholders::_3)
        );

    /* regist device property changed callback */
    mDeviceMgr.registDevicePropertyChangedCallback(
        std::bind(
            &DeviceDataChannel::onPropertyChanged,
            this,
            std::placeholders::_1,
            std::placeholders::_2,
            std::placeholders::_3)
        );
    return 0;
}

void DeviceDataChannel::onStateChanged(std::string did, std::string devName, int state)
{
    LOGTT();

    switch (state) {
        case 1:
            { /* online */
                std::shared_ptr<InstancePayload> payload = std::make_shared<InstancePayload>();
                payload->mInsName = innerOfInsname(did);
                payload->mClsName = devName;
                mH.sendMessage(mH.obtainMessage(RET_INSTANCE_ADD, payload));
            }
            break;
        case 2: /* offline */
            {
                std::shared_ptr<InstancePayload> payload = std::make_shared<InstancePayload>();
                payload->mInsName = innerOfInsname(did);
                mH.sendMessage(mH.obtainMessage(RET_INSTANCE_DEL, payload));
            }
            break;
    }
}

void DeviceDataChannel::onPropertyChanged(std::string did, std::string proKey, std::string proVal)
{
    LOGTT();

    std::shared_ptr<InstancePayload> payload = std::make_shared<InstancePayload>();
    payload->mInsName = innerOfInsname(did);
    payload->mSlots.push_back(InstancePayload::SlotInfo(proKey, proVal));
    mH.sendMessage(mH.obtainMessage(RET_INSTANCE_PUT, payload));
}

bool DeviceDataChannel::send(int action, std::shared_ptr<Payload> data)
{
    LOGTT();
    if (action == PT_INSTANCE_PAYLOAD) {
        std::shared_ptr<InstancePayload> payload(std::dynamic_pointer_cast<InstancePayload>(data));
        mDeviceMgr.setProperty(
            outerOfInsname(payload->mInsName),
            payload->mSlots[0].nName,
            payload->mSlots[0].nValue);
    }
    return true;
}

ElinkDeviceDataChannel::ElinkDeviceDataChannel()
{
}

ElinkDeviceDataChannel::~ElinkDeviceDataChannel()
{
}

int ElinkDeviceDataChannel::init()
{
    DeviceDataChannel::init();

    /* regist device profile sync callback */
    mDeviceMgr.registDeviceProfileSyncCallback(
        std::bind(
            &ElinkDeviceDataChannel::onProfileSync,
            this,
            std::placeholders::_1,
            std::placeholders::_2)
        );
    return 0;
}

bool ElinkDeviceDataChannel::_ParsePropValue(const char *cpropval, Slot &slot)
{
    char *propval = (char*)cpropval;
    char *t_and = strstr(propval, "and");
    if (!t_and)
        return true;
    char lside[64] = { 0 };
    char rside[64] = { 0 };
    strncpy(lside, propval, t_and - propval);
    strcpy(rside, t_and+3);

    if (lside[0]) {
        char *egt = strstr(lside, ">=");
        if (!egt)
            return false;
        slot.mMin = std::string(egt + 2);
        stringTrim(slot.mMin);
    }

    if (rside[0]) {
        char *elt = strstr(propval, "<=");
        if (!elt)
            return false;
        slot.mMax = std::string(elt + 2);
        stringTrim(slot.mMax);
    }
    return true;
}

void ElinkDeviceDataChannel::onProfileSync(std::string devName, std::string jsondoc)
{
    // LOGD("jsondoc:\n%s\n", jsondoc.c_str());
    rapidjson::Document doc;
    doc.Parse<0>(jsondoc.c_str());
    if (doc.HasParseError()) {
        rapidjson::ParseErrorCode code = doc.GetParseError();
        LOGE("rapidjson parse error[%d]\n", code);
        return;
    }

    if (!doc.HasMember("status") && !doc.HasMember("result")) {
        LOGE("rapidjson parse error!\n");
        return;
    }

    rapidjson::Value &result = doc["result"];
    if (!result.HasMember("profile")) {
        LOGE("rapidjson parse error, no profile key!\n");
        return;
    }

    rapidjson::Value &profile = result["profile"];

    /* elink v0.0.7 */
    std::string profileVersion("0.0.7");

    std::shared_ptr<ClassPayload> payload = std::make_shared<ClassPayload>(devName, "DEVICE", profileVersion);

    if (!profile.IsObject()) {
        LOGE("parse profile error, not object!\n");
        return;
    }
    rapidjson::Value::ConstMemberIterator itprofile;
    for (itprofile = profile.MemberBegin(); itprofile != profile.MemberEnd(); ++itprofile) {
        LOGI("%s is %d\n", itprofile->name.GetString(), itprofile->value.GetType());
        Slot &slot = payload->makeSlot(itprofile->name.GetString());
        if (!itprofile->value.IsObject()) {
            LOGE("parse profile %s.%s error, value is not object!\n", devName.c_str(), itprofile->name.GetString());
            return;
        }
        if (!itprofile->value.HasMember("type") && !itprofile->value.HasMember("range")) {
            LOGE("parse profile %s.%s.type|range error, not found type!\n", devName.c_str(), itprofile->name.GetString());
            return;
        }
        const rapidjson::Value &type = itprofile->value["type"];
        const rapidjson::Value &range = itprofile->value["range"];
        const char *typestr = type.GetString();
        if (!strncmp(typestr, "enum", 4)) {
            if (!range.IsObject()) {
                LOGE("parse profile %s.%s.range error, not object!\n", devName.c_str(), itprofile->name.GetString());
                return;
            }
            std::string allowlist;
            rapidjson::Value::ConstMemberIterator itrange;
            bool symb2int = true;
            for (itrange = range.MemberBegin(); itrange != range.MemberEnd(); ++itrange) {
                LOGI("range[%s]: %s is %d\n", itprofile->name.GetString(), itrange->name.GetString(), itrange->value.GetType());
                const char *enumrange = itrange->name.GetString();
                /* TODO */
                if (!isdigit(enumrange[0]))
                    symb2int = false;
                allowlist.append(" ").append(itrange->name.GetString());
            }
            if (symb2int)
                slot.mType = ST_NUMBER;
            else
                slot.mType = ST_SYMBOL;
            slot.mAllowList = stringTrim(allowlist);
        } else if (!strncmp(typestr, "int", 3)) {
            if (!range.IsString()) {
                LOGE("parse profile %s.%s.range error, not String!\n", devName.c_str(), itprofile->name.GetString());
                return;
            }
            if (!_ParsePropValue(range.GetString(), slot)) {
                LOGE("parse profile %s.%s.range error!\n", devName.c_str(), itprofile->name.GetString());
                return;
            }
            slot.mType = ST_NUMBER;
        } else if (!strncmp(typestr, "float", 5)) {
            if (!range.IsString()) {
                LOGE("parse profile %s.%s.range error, not String!\n", devName.c_str(), itprofile->name.GetString());
                return;
            }
            if (!_ParsePropValue(range.GetString(), slot)) {
                LOGE("parse profile %s.%s.range error!\n", devName.c_str(), itprofile->name.GetString());
                return;
            }
            slot.mType = ST_NUMBER;
        } else {
            LOGE("parse profile error, not support type!\n");
            return;
        }
    }
    mH.sendMessage(mH.obtainMessage(RET_CLASS_SYNC, payload));
}

} /* namespace HB */
