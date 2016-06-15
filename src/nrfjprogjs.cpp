#include <nan.h>

#include <vector>

#include "nrfjprogjs.h"
#include "nrfjprog_common.h"

#include "keilhexfile.h"
#include "dllfunc.h"

#include <iostream>

Nan::Persistent<v8::Function> DebugProbe::constructor;
DllFunctionPointersType DebugProbe::dll_function;
char DebugProbe::dll_path[COMMON_MAX_PATH] = {'\0'};
char DebugProbe::jlink_path[COMMON_MAX_PATH] = {'\0'};
bool DebugProbe::loaded = false;
int DebugProbe::error = errorcodes::JsSuccess;
uint32_t DebugProbe::emulatorSpeed = 1000;
uint32_t DebugProbe::versionMagicNumber = 0x46D8A517; //YGGDRAIL
                                              
v8::Local<v8::Object> ProbeInfo::ToJs()
{
    Nan::EscapableHandleScope scope;
    v8::Local<v8::Object> obj = Nan::New<v8::Object>();

    Utility::Set(obj, "serialNumber", ConversionUtility::toJsNumber(serial_number));
    Utility::Set(obj, "family", ConversionUtility::toJsNumber(family));

    return scope.Escape(obj);
}

NAN_MODULE_INIT(DebugProbe::Init)
{
    v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);
    tpl->SetClassName(Nan::New("DebugProbe").ToLocalChecked());
    tpl->InstanceTemplate()->SetInternalFieldCount(1);

    init(tpl);

    constructor.Reset(Nan::GetFunction(tpl).ToLocalChecked());
    Nan::Set(target, Nan::New("DebugProbe").ToLocalChecked(), Nan::GetFunction(tpl).ToLocalChecked());
}

NAN_METHOD(DebugProbe::New)
{
    if (info.IsConstructCall()) {
        auto obj = new DebugProbe();
        obj->Wrap(info.This());
        info.GetReturnValue().Set(info.This());
    } else {
        v8::Local<v8::Function> cons = Nan::New(constructor);
        info.GetReturnValue().Set(cons->NewInstance());
    }
}

DebugProbe::DebugProbe()
{
    if (loaded)
    {
        return;
    }

    NrfjprogErrorCodesType dll_find_result = OSFilesFindDll(dll_path, COMMON_MAX_PATH);
    NrfjprogErrorCodesType jlink_dll_find_result = OSFilesFindJLink(jlink_path, COMMON_MAX_PATH);
    NrfjprogErrorCodesType dll_load_result = DllLoad(dll_path, &dll_function);
    loaded = false;

    if (dll_find_result != Success)
    {
        error = errorcodes::CouldNotLoadDLL;
    }
    else if (jlink_dll_find_result != Success)
    {
        error = errorcodes::CouldNotLoadDLL;
    }
    else if (dll_load_result != Success)
    {
        error = errorcodes::CouldNotLoadDLL;
    }
    else
    {
        error = errorcodes::JsSuccess;
        loaded = true;
    }
}

DebugProbe::~DebugProbe()
{
    loaded = false;
    DllFree();
}

void DebugProbe::closeBeforeExit()
{
    dll_function.sys_reset();
    dll_function.go();

    dll_function.disconnect_from_emu();

    dll_function.close_dll();
}

void DebugProbe::init(v8::Local<v8::FunctionTemplate> tpl)
{
    Nan::SetPrototypeMethod(tpl, "program", Program);
    Nan::SetPrototypeMethod(tpl, "getVersion", GetVersion);
    Nan::SetPrototypeMethod(tpl, "getSerialNumbers", GetSerialnumbers);
}

device_family_t DebugProbe::getFamily(const uint32_t serialnumber)
{
    device_version_t deviceType = UNKNOWN;

    dll_function.connect_to_emu_with_snr(serialnumber, emulatorSpeed);

    dll_function.read_device_version(&deviceType);

    dll_function.disconnect_from_emu();

    switch (deviceType)
    {
        case NRF51_XLR1:
        case NRF51_XLR2:
        case NRF51_XLR3:
        case NRF51_L3:
        case NRF51_XLR3P:
        case NRF51_XLR3LC:
            return NRF51_FAMILY;
        case NRF52_FP1_ENGA:
        case NRF52_FP1_ENGB:
        case NRF52_FP1:
        case UNKNOWN:
        default:
            return NRF52_FAMILY;
    }
}

#pragma region GetSerialnumbers
NAN_METHOD(DebugProbe::GetSerialnumbers)
{
    auto obj = Nan::ObjectWrap::Unwrap<DebugProbe>(info.Holder());
    auto argumentCount = 0;
	v8::Local<v8::Function> callback;

    try
    {
		callback = ConversionUtility::getCallbackFunction(info[argumentCount]);
		argumentCount++;
    }
    catch (std::string error)
    {
        auto message = ErrorMessage::getTypeErrorMessage(argumentCount, error);
        Nan::ThrowTypeError(message);
        return;
    }

    auto baton = new GetSerialnumbersBaton(callback);

    uv_queue_work(uv_default_loop(), baton->req, GetSerialnumbers, reinterpret_cast<uv_after_work_cb>(AfterGetSerialnumbers));
}


void DebugProbe::GetSerialnumbers(uv_work_t *req)
{
    auto baton = static_cast<GetSerialnumbersBaton*>(req->data);

    if (!loaded)
    {
        baton->result = error;
        return;
    }
    auto const probe_count_max = MAX_SERIAL_NUMBERS;
    uint32_t probe_count = 0;

    uint32_t _probes[MAX_SERIAL_NUMBERS];

    // Find nRF51 devices available
    baton->result = dll_function.open_dll(jlink_path, nullptr, NRF51_FAMILY);

    if (baton->result != SUCCESS)
    {
        baton->result = errorcodes::CouldNotOpenDevice;
        return;
    }

    baton->result = dll_function.enum_emu_snr(_probes, probe_count_max, &probe_count);
    dll_function.close_dll();

    if (baton->result != SUCCESS)
    {
        baton->result = errorcodes::CouldNotCallFunction;
        return;
    }

    for (uint32_t i = 0; i < probe_count; i++)
    {
        device_family_t family = getFamily(_probes[i]);

        baton->probes.push_back(new ProbeInfo(_probes[i], family));
    }
}

void DebugProbe::AfterGetSerialnumbers(uv_work_t *req)
{
    Nan::HandleScope scope;

    auto baton = static_cast<GetSerialnumbersBaton*>(req->data);
    v8::Local<v8::Value> argv[2];

    if (baton->result != errorcodes::JsSuccess)
    {
        argv[0] = ErrorMessage::getErrorMessage(baton->result, "getting serialnumbers");
        argv[1] = Nan::Undefined();
    }
    else
    {
        argv[0] = Nan::Undefined();
        v8::Local<v8::Array> serialNumbers = Nan::New<v8::Array>();

        for (uint32_t i = 0; i < baton->probes.size(); ++i)
        {
            Nan::Set(serialNumbers, Nan::New<v8::Integer>(i), baton->probes[i]->ToJs());
        }

        argv[1] = serialNumbers;
    }

    baton->callback->Call(2, argv);

    delete baton;
}
#pragma endregion GetSerialnumbers

#pragma region Program
NAN_METHOD(DebugProbe::Program)
{
    auto obj = Nan::ObjectWrap::Unwrap<DebugProbe>(info.Holder());
    auto argumentCount = 0;

    uint32_t serialNumber;
    uint32_t family = ANY_FAMILY;
    std::string filename;
    v8::Local<v8::Object> filenameObject;
    v8::Local<v8::Function> callback;

    try
    {
        serialNumber = ConversionUtility::getNativeUint32(info[argumentCount]);
        argumentCount++;
        
        if (info.Length() == 3)
        {
            filenameObject = ConversionUtility::getJsObject(info[argumentCount]);
            argumentCount++;
        }
        else if (info.Length() == 4)
        {
            family = ConversionUtility::getNativeUint32(info[argumentCount]);
            argumentCount++;
                        
            filename = ConversionUtility::getNativeString(info[argumentCount]);
            argumentCount++;
        }
        else
        {
            throw "parameter count";
        }

        callback = ConversionUtility::getCallbackFunction(info[argumentCount]);
        argumentCount++;
    }
    catch (std::string error)
    {
        auto message = ErrorMessage::getTypeErrorMessage(argumentCount, error);
        Nan::ThrowTypeError(message);
        return;
    }

    auto baton = new ProgramBaton(callback);
    baton->serialnumber = serialNumber;
    baton->family = (device_family_t)family;

    if (info.Length() == 4)
    {
        baton->filename = filename;
    }
    else
    {
        if (Utility::Has(filenameObject, NRF51_FAMILY))
        {
            baton->filenameMap[NRF51_FAMILY] = ConversionUtility::getNativeString(Utility::Get(filenameObject, NRF51_FAMILY));
        }

        if (Utility::Has(filenameObject, NRF52_FAMILY))
        {
            baton->filenameMap[NRF52_FAMILY] = ConversionUtility::getNativeString(Utility::Get(filenameObject, NRF52_FAMILY));
        }
    }

    uv_queue_work(uv_default_loop(), baton->req, Program, reinterpret_cast<uv_after_work_cb>(AfterProgram));
}

void DebugProbe::Program(uv_work_t *req)
{
    auto baton = static_cast<ProgramBaton*>(req->data);
    
    if (!loaded)
    {
        baton->result = error;
        return;
    }

    if (baton->family != ANY_FAMILY)
    {
        baton->result = dll_function.open_dll(jlink_path, nullptr, baton->family);
    }
    else
    {
        baton->result = dll_function.open_dll(jlink_path, nullptr, NRF51_FAMILY);
        baton->family = getFamily(baton->serialnumber);

        if (baton->family != NRF51_FAMILY)
        {
            dll_function.close_dll();
            baton->result = dll_function.open_dll(jlink_path, nullptr, baton->family);
        }

        baton->filename = baton->filenameMap[baton->family];
    }

    if (baton->result != SUCCESS)
    {
        baton->result = errorcodes::CouldNotOpenDevice;
        return;
    }

    baton->result = dll_function.connect_to_emu_with_snr(baton->serialnumber, emulatorSpeed);

    if (baton->result != SUCCESS)
    {
        baton->result = errorcodes::CouldNotConnectToDevice;
        return;
    }

    KeilHexFile program_hex;

    KeilHexFile::Status status = program_hex.open(baton->filename.c_str());

    uint32_t code_size = 512 * 1024;
    uint8_t *code = new uint8_t[code_size];

    if (program_hex.nand_read(0, code, code_size) != KeilHexFile::SUCCESS)
    {
        baton->result = errorcodes::CouldNotCallFunction;
        closeBeforeExit();
        delete[] code;
        return;
    }

    if (dll_function.erase_all() != SUCCESS)
    {
        baton->result = errorcodes::CouldNotErase;
        closeBeforeExit();
        delete[] code;
        return;
    }

    uint32_t foundAddress;
    uint32_t bytesFound;

    program_hex.find_contiguous(0, foundAddress, bytesFound);

    do
    {
        baton->result = dll_function.write(foundAddress, (const uint8_t *)&code[foundAddress], bytesFound, true);

        if (baton->result != SUCCESS)
        {
            baton->result = errorcodes::CouldNotProgram;
            break;
        }

        program_hex.find_contiguous(foundAddress + bytesFound, foundAddress, bytesFound);
    } while (bytesFound != 0);

    closeBeforeExit();
    delete[] code;
}

void DebugProbe::AfterProgram(uv_work_t *req)
{
    Nan::HandleScope scope;

    auto baton = static_cast<ProgramBaton*>(req->data);
    v8::Local<v8::Value> argv[1];

    if (baton->result != errorcodes::JsSuccess)
    {
        argv[0] = ErrorMessage::getErrorMessage(baton->result, "programming");
    }
    else
    {
        argv[0] = Nan::Undefined();
    }

    baton->callback->Call(1, argv);

    delete baton;
}
#pragma endregion Program

#pragma region GetVersion
NAN_METHOD(DebugProbe::GetVersion)
{
    auto obj = Nan::ObjectWrap::Unwrap<DebugProbe>(info.Holder());
    auto argumentCount = 0;

    uint32_t serialNumber;
    uint32_t family = ANY_FAMILY;
    v8::Local<v8::Function> callback;

    try
    {
        serialNumber = ConversionUtility::getNativeUint32(info[argumentCount]);
        argumentCount++;

        if (info.Length() == 3)
        {
            family = ConversionUtility::getNativeUint32(info[argumentCount]);
            argumentCount++;
        }

        callback = ConversionUtility::getCallbackFunction(info[argumentCount]);
        argumentCount++;
    }
    catch (std::string error)
    {
        auto message = ErrorMessage::getTypeErrorMessage(argumentCount, error);
        Nan::ThrowTypeError(message);
        return;
    }

    auto baton = new GetVersionBaton(callback);
    baton->serialnumber = serialNumber;
    baton->family = (device_family_t)family;

    uv_queue_work(uv_default_loop(), baton->req, GetVersion, reinterpret_cast<uv_after_work_cb>(AfterGetVersion));
}

uint32_t getNumber(const uint8_t *data, const int offset, const int length)
{
    uint32_t _r = 0;

    for (int i = 0; i < length; i++)
    {
        _r += (data[offset + i] << (i * 8));
    }

    return _r;
}

void DebugProbe::GetVersion(uv_work_t *req)
{
    auto baton = static_cast<GetVersionBaton*>(req->data);

    if (!loaded)
    {
        baton->result = error;
        return;
    }

    device_family_t family = baton->family;

    if (family == ANY_FAMILY)
    {
        family = getFamily(baton->serialnumber);
    }

    // Find nRF51 devices available
    baton->result = dll_function.open_dll(jlink_path, nullptr, family);

    if (baton->result != SUCCESS)
    {
        baton->result = errorcodes::CouldNotOpenDevice;
        return;
    }

    baton->result = dll_function.connect_to_emu_with_snr(baton->serialnumber, emulatorSpeed);

    if (baton->result != SUCCESS)
    {
        baton->result = errorcodes::CouldNotConnectToDevice;
        return;
    }                                           

    //TODO: Get actual version (i.e. correct address)
    baton->result = dll_function.read(0x20000, baton->versionData, 16);

    if (baton->result != SUCCESS)
    {
        baton->result = errorcodes::CouldNotRead;
        closeBeforeExit();
        return;
    }

    const uint32_t magicNumber = getNumber(baton->versionData, 0, 4);

    if (magicNumber != versionMagicNumber)
    {
        std::cout << std::hex << magicNumber << std::endl;
        baton->result = errorcodes::WrongMagicNumber;
        closeBeforeExit();
        return;
    }

    const uint8_t length = getNumber(baton->versionData, 15, 1);

    if (length > 0)
    {
        uint8_t *versiontext = new uint8_t[length + 1];
        versiontext[length] = '\0';
        baton->result = dll_function.read(0x20000 + 16, versiontext, length);

        if (baton->result != SUCCESS)
        {
            baton->result = errorcodes::CouldNotRead;
            delete[] versiontext;
            closeBeforeExit();
            return;
        }

        baton->versionText = std::string((char *)versiontext);

        delete[] versiontext;
    }

    closeBeforeExit();
}

#include <sstream>

void DebugProbe::AfterGetVersion(uv_work_t *req)
{
    Nan::HandleScope scope;

    auto baton = static_cast<GetVersionBaton*>(req->data);
    v8::Local<v8::Value> argv[2];

    if (baton->result != errorcodes::JsSuccess)
    {
        argv[0] = ErrorMessage::getErrorMessage(baton->result, "getting version");
        argv[1] = Nan::Undefined();
    }
    else
    {
        argv[0] = Nan::Undefined();

        v8::Local<v8::Object> obj = Nan::New<v8::Object>();

        uint32_t magic = getNumber(baton->versionData, 0, 4);
        uint8_t firmwareID = getNumber(baton->versionData, 4, 1);
        uint32_t hash = getNumber(baton->versionData, 8, 4);
        uint8_t major = getNumber(baton->versionData, 12, 1);
        uint8_t minor = getNumber(baton->versionData, 13, 1);
        uint8_t bug = getNumber(baton->versionData, 14, 1);
    
        std::stringstream versionstring;

        versionstring << (int)major << "." << (int)minor << "." << (int)bug << " " << std::hex << hash;

        std::stringstream magicString;

        magicString << std::hex << magic;

        Utility::Set(obj, "Magic", ConversionUtility::toJsString(magicString.str()));
        Utility::Set(obj, "FirmwareID", ConversionUtility::toJsNumber(firmwareID));
        Utility::Set(obj, "Major", ConversionUtility::toJsNumber(major));
        Utility::Set(obj, "Minor", ConversionUtility::toJsNumber(minor));
        Utility::Set(obj, "Bug", ConversionUtility::toJsNumber(bug));
        Utility::Set(obj, "Hash", ConversionUtility::toJsNumber(hash));
        Utility::Set(obj, "String", ConversionUtility::toJsString(versionstring.str()));
        Utility::Set(obj, "Text", ConversionUtility::toJsString(baton->versionText));

        argv[1] = obj;
    }

    baton->callback->Call(2, argv);

    delete baton;
}
#pragma endregion GetVersion

extern "C" {
    void initConsts(Nan::ADDON_REGISTER_FUNCTION_ARGS_TYPE target)
    {/*
        NODE_DEFINE_CONSTANT(target, R0);
        NODE_DEFINE_CONSTANT(target, R1);
        NODE_DEFINE_CONSTANT(target, R2);
        NODE_DEFINE_CONSTANT(target, R3);
        NODE_DEFINE_CONSTANT(target, R4);
        NODE_DEFINE_CONSTANT(target, R5);
        NODE_DEFINE_CONSTANT(target, R6);
        NODE_DEFINE_CONSTANT(target, R7);
        NODE_DEFINE_CONSTANT(target, R8);
        NODE_DEFINE_CONSTANT(target, R9);
        NODE_DEFINE_CONSTANT(target, R10);
        NODE_DEFINE_CONSTANT(target, R11);
        NODE_DEFINE_CONSTANT(target, R12);
        NODE_DEFINE_CONSTANT(target, R13);
        NODE_DEFINE_CONSTANT(target, R14);
        NODE_DEFINE_CONSTANT(target, R15);
        NODE_DEFINE_CONSTANT(target, XPSR);
        NODE_DEFINE_CONSTANT(target, MSP);
        NODE_DEFINE_CONSTANT(target, PSP);

        NODE_DEFINE_CONSTANT(target, RAM_OFF);
        NODE_DEFINE_CONSTANT(target, RAM_ON);

        NODE_DEFINE_CONSTANT(target, NONE);
        NODE_DEFINE_CONSTANT(target, REGION_0);
        NODE_DEFINE_CONSTANT(target, ALL);
        NODE_DEFINE_CONSTANT(target, BOTH);

        NODE_DEFINE_CONSTANT(target, NO_REGION_0);
        NODE_DEFINE_CONSTANT(target, FACTORY);
        NODE_DEFINE_CONSTANT(target, USER);

        NODE_DEFINE_CONSTANT(target, UNKNOWN);
        NODE_DEFINE_CONSTANT(target, NRF51_XLR1);
        NODE_DEFINE_CONSTANT(target, NRF51_XLR2);
        NODE_DEFINE_CONSTANT(target, NRF51_XLR3);
        NODE_DEFINE_CONSTANT(target, NRF51_L3);
        NODE_DEFINE_CONSTANT(target, NRF51_XLR3P);
        NODE_DEFINE_CONSTANT(target, NRF51_XLR3LC);
        NODE_DEFINE_CONSTANT(target, NRF52_FP1_ENGA);
        NODE_DEFINE_CONSTANT(target, NRF52_FP1_ENGB);
        NODE_DEFINE_CONSTANT(target, NRF52_FP1);
        */
        NODE_DEFINE_CONSTANT(target, NRF51_FAMILY);
        NODE_DEFINE_CONSTANT(target, NRF52_FAMILY);
        /*
        NODE_DEFINE_CONSTANT(target, UP_DIRECTION);
        NODE_DEFINE_CONSTANT(target, DOWN_DIRECTION);

        NODE_DEFINE_CONSTANT(target, SUCCESS);
        NODE_DEFINE_CONSTANT(target, OUT_OF_MEMORY);
        NODE_DEFINE_CONSTANT(target, INVALID_OPERATION);
        NODE_DEFINE_CONSTANT(target, INVALID_PARAMETER);
        NODE_DEFINE_CONSTANT(target, INVALID_DEVICE_FOR_OPERATION);
        NODE_DEFINE_CONSTANT(target, WRONG_FAMILY_FOR_DEVICE);
        NODE_DEFINE_CONSTANT(target, EMULATOR_NOT_CONNECTED);
        NODE_DEFINE_CONSTANT(target, CANNOT_CONNECT);
        NODE_DEFINE_CONSTANT(target, LOW_VOLTAGE);
        NODE_DEFINE_CONSTANT(target, NO_EMULATOR_CONNECTED);
        NODE_DEFINE_CONSTANT(target, NVMC_ERROR);
        NODE_DEFINE_CONSTANT(target, NOT_AVAILABLE_BECAUSE_PROTECTION);
        NODE_DEFINE_CONSTANT(target, JLINKARM_DLL_NOT_FOUND);
        NODE_DEFINE_CONSTANT(target, JLINKARM_DLL_COULD_NOT_BE_OPENED);
        NODE_DEFINE_CONSTANT(target, JLINKARM_DLL_ERROR);
        NODE_DEFINE_CONSTANT(target, JLINKARM_DLL_TOO_OLD);
        NODE_DEFINE_CONSTANT(target, NRFJPROG_SUB_DLL_NOT_FOUND);
        NODE_DEFINE_CONSTANT(target, NRFJPROG_SUB_DLL_COULD_NOT_BE_OPENED);
        NODE_DEFINE_CONSTANT(target, NOT_IMPLEMENTED_ERROR);

        NODE_DEFINE_CONSTANT(target, Success);
        NODE_DEFINE_CONSTANT(target, NrfjprogError);
        NODE_DEFINE_CONSTANT(target, NrfjprogOutdatedError);
        NODE_DEFINE_CONSTANT(target, MemoryAllocationError);
        NODE_DEFINE_CONSTANT(target, InvalidArgumentError);
        NODE_DEFINE_CONSTANT(target, InsufficientArgumentsError);
        NODE_DEFINE_CONSTANT(target, IncompatibleArgumentsError);
        NODE_DEFINE_CONSTANT(target, DuplicatedArgumentsError);
        NODE_DEFINE_CONSTANT(target, NoOperationError);
        NODE_DEFINE_CONSTANT(target, UnavailableOperationBecauseProtectionError);
        NODE_DEFINE_CONSTANT(target, UnavailableOperationInFamilyError);
        NODE_DEFINE_CONSTANT(target, WrongFamilyForDeviceError);
        NODE_DEFINE_CONSTANT(target, NrfjprogDllNotFoundError);
        NODE_DEFINE_CONSTANT(target, NrfjprogDllLoadFailedError);
        NODE_DEFINE_CONSTANT(target, NrfjprogDllFunctionLoadFailedError);
        NODE_DEFINE_CONSTANT(target, NrfjprogDllNotImplementedError);
        NODE_DEFINE_CONSTANT(target, NrfjprogIniNotFoundError);
        NODE_DEFINE_CONSTANT(target, NrfjprogIniFormatError);
        NODE_DEFINE_CONSTANT(target, JLinkARMDllNotFoundError);
        NODE_DEFINE_CONSTANT(target, JLinkARMDllInvalidError);
        NODE_DEFINE_CONSTANT(target, JLinkARMDllFailedToOpenError);
        NODE_DEFINE_CONSTANT(target, JLinkARMDllError);
        NODE_DEFINE_CONSTANT(target, JLinkARMDllTooOldError);
        NODE_DEFINE_CONSTANT(target, InvalidSerialNumberError);
        NODE_DEFINE_CONSTANT(target, NoDebuggersError);
        NODE_DEFINE_CONSTANT(target, NotPossibleToConnectError);
        NODE_DEFINE_CONSTANT(target, LowVoltageError);
        NODE_DEFINE_CONSTANT(target, FileNotFoundError);
        NODE_DEFINE_CONSTANT(target, InvalidHexFileError);
        NODE_DEFINE_CONSTANT(target, FicrReadError);
        NODE_DEFINE_CONSTANT(target, WrongArgumentError);
        NODE_DEFINE_CONSTANT(target, VerifyError);
        NODE_DEFINE_CONSTANT(target, NoWritePermissionError);
        NODE_DEFINE_CONSTANT(target, NVMCOperationError);
        NODE_DEFINE_CONSTANT(target, FlashNotErasedError);
        NODE_DEFINE_CONSTANT(target, RamIsOffError);
        NODE_DEFINE_CONSTANT(target, FicrOperationWarning);
        NODE_DEFINE_CONSTANT(target, UnalignedPageEraseWarning);
        NODE_DEFINE_CONSTANT(target, NoLogWarning);
        NODE_DEFINE_CONSTANT(target, UicrWriteOperationWithoutEraseWarning);
        */

        NODE_DEFINE_CONSTANT(target, JsSuccess);
        NODE_DEFINE_CONSTANT(target, CouldNotOpenDevice);
        NODE_DEFINE_CONSTANT(target, CouldNotLoadDLL);
        NODE_DEFINE_CONSTANT(target, CouldNotCallFunction);
    }

    NAN_MODULE_INIT(init)
    {
        initConsts(target);
        DebugProbe::Init(target);
    }
}

NODE_MODULE(pc_nrfjprog, init);
