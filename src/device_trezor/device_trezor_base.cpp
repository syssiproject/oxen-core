// Copyright (c) 2017-2019, The Monero Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#include "device_trezor_base.hpp"

#include "common/lock.h"
#include "common/string_util.h"
#include "common/exception.h"
#include "epee/memwipe.h"

namespace hw::trezor {

#ifdef WITH_DEVICE_TREZOR

static auto logcat = log::Cat("device.trezor");
#define TREZOR_BIP44_HARDENED_ZERO 0x80000000

const uint32_t device_trezor_base::DEFAULT_BIP44_PATH[] = {0x8000002c, 0x80000080};

device_trezor_base::device_trezor_base() :
        m_callback(nullptr), m_last_msg_type(messages::MessageType_Success) {
#ifdef WITH_TREZOR_DEBUGGING
    m_debug = false;
#endif
}

device_trezor_base::~device_trezor_base() {
    try {
        disconnect();
        release();
    } catch (std::exception const& e) {
        log::error(logcat, "Could not disconnect and release: {}", e.what());
    }
}

/* ======================================================================= */
/*                              SETUP/TEARDOWN                             */
/* ======================================================================= */

bool device_trezor_base::reset() {
    return false;
}

bool device_trezor_base::set_name(std::string_view name) {
    m_full_name = name;

    if (auto delim = name.find(':'); delim != std::string::npos && delim + 1 < name.length())
        this->name = name.substr(delim + 1);
    else
        this->name = "";

    return true;
}

std::string device_trezor_base::get_name() const {
    if (m_full_name.empty())
        return "<disconnected:" + name + ">";
    return m_full_name;
}

bool device_trezor_base::init() {
    if (!release()) {
        log::error(logcat, "Release failed");
        return false;
    }

    return true;
}

bool device_trezor_base::release() {
    try {
        disconnect();
        return true;

    } catch (std::exception const& e) {
        log::error(logcat, "Release exception: {}", e.what());
        return false;
    }
}

bool device_trezor_base::connect() {
    disconnect();

    // Enumerate all available devices
    std::lock_guard lock{device_locker};
    try {
        hw::trezor::t_transport_vect trans;

        log::debug(logcat, "Enumerating Trezor devices...");
        enumerate(trans);
        sort_transports_by_env(trans);

        log::debug(logcat, "Enumeration yielded {} Trezor devices", trans.size());
        for (auto& cur : trans) {
            log::debug(logcat, "  device: {}", *(cur.get()));
        }

        for (auto& cur : trans) {
            std::string cur_path = cur->get_path();
            if (cur_path.starts_with(name)) {
                log::debug(logcat, "Device Match: {}", cur_path);
                m_transport = cur;
                break;
            }
        }

        if (!m_transport) {
            log::error(logcat, "No matching Trezor device found. Device specifier: \"{}\"", name);
            return false;
        }

        m_transport->open();

#ifdef WITH_TREZOR_DEBUGGING
        setup_debug();
#endif
        return true;

    } catch (std::exception const& e) {
        log::error(logcat, "Open exception: {}", e.what());
        return false;
    }
}

bool device_trezor_base::disconnect() {
    std::lock_guard lock{device_locker};
    m_device_session_id.clear();
    m_features.reset();

    if (m_transport) {
        try {
            m_transport->close();
            m_transport = nullptr;

        } catch (std::exception const& e) {
            log::error(logcat, "Disconnect exception: {}", e.what());
            m_transport = nullptr;
            return false;
        }
    }

#ifdef WITH_TREZOR_DEBUGGING
    if (m_debug_callback) {
        m_debug_callback->on_disconnect();
        m_debug_callback = nullptr;
    }
#endif
    return true;
}

/* ======================================================================= */
/*  LOCKER                                                                 */
/* ======================================================================= */

// lock the device for a long sequence
void device_trezor_base::lock() {
    log::trace(logcat, "Ask for LOCKING for device {} in thread ", name);
    device_locker.lock();
    log::trace(logcat, "Device {} LOCKed", name);
}

// lock the device for a long sequence
bool device_trezor_base::try_lock() {
    log::trace(logcat, "Ask for LOCKING(try) for device {} in thread ", name);
    bool r = device_locker.try_lock();
    if (r) {
        log::trace(logcat, "Device {} LOCKed(try)", name);
    } else {
        log::debug(logcat, "Device {} not LOCKed(try)", name);
    }
    return r;
}

// unlock the device
void device_trezor_base::unlock() {
    log::trace(logcat, "Ask for UNLOCKING for device {} in thread ", name);
    device_locker.unlock();
    log::trace(logcat, "Device {} UNLOCKed", name);
}

/* ======================================================================= */
/*  Helpers                                                                */
/* ======================================================================= */

void device_trezor_base::require_connected() const {
    if (!m_transport) {
        throw exc::NotConnectedException();
    }
}

void device_trezor_base::require_initialized() const {
    if (!m_features) {
        throw exc::TrezorException("Device state not initialized");
    }

    if (m_features->has_bootloader_mode() && m_features->bootloader_mode()) {
        throw exc::TrezorException("Device is in the bootloader mode");
    }

    if (m_features->has_firmware_present() && !m_features->firmware_present()) {
        throw exc::TrezorException("Device has no firmware loaded");
    }

    // Hard requirement on initialized field, has to be there.
    if (!m_features->has_initialized() || !m_features->initialized()) {
        throw exc::TrezorException("Device is not initialized");
    }
}

void device_trezor_base::call_ping_unsafe() {
    auto pingMsg = std::make_shared<messages::management::Ping>();
    pingMsg->set_message("PING");

    auto success =
            client_exchange<messages::common::Success>(pingMsg);  // messages::MessageType_Success
    log::debug(logcat, "Ping response {}", success->message());
    (void)success;
}

void device_trezor_base::test_ping() {
    require_connected();

    try {
        call_ping_unsafe();

    } catch (exc::TrezorException const& e) {
        log::info(logcat, "Trezor does not respond: {}", e.what());
        throw exc::DeviceNotResponsiveException(std::string("Trezor not responding: ") + e.what());
    }
}

void device_trezor_base::write_raw(const google::protobuf::Message* msg) {
    require_connected();
    CHECK_AND_ASSERT_THROW_MES(msg, "Empty message");
    get_transport()->write(*msg);
}

GenericMessage device_trezor_base::read_raw() {
    require_connected();
    std::shared_ptr<google::protobuf::Message> msg_resp;
    hw::trezor::messages::MessageType msg_resp_type;

    get_transport()->read(msg_resp, &msg_resp_type);
    return GenericMessage(msg_resp_type, msg_resp);
}

GenericMessage device_trezor_base::call_raw(const google::protobuf::Message* msg) {
    write_raw(msg);
    return read_raw();
}

bool device_trezor_base::message_handler(GenericMessage& input) {
    // Later if needed this generic message handler can be replaced by a pointer to
    // a protocol message handler which by default points to the device class which implements
    // the default handler.

    if (m_last_msg_type == messages::MessageType_ButtonRequest) {
        on_button_pressed();
    }
    m_last_msg_type = input.m_type;

    switch (input.m_type) {
        case messages::MessageType_ButtonRequest:
            on_button_request(
                    input, dynamic_cast<const messages::common::ButtonRequest*>(input.m_msg.get()));
            return true;
        case messages::MessageType_PassphraseRequest:
            on_passphrase_request(
                    input,
                    dynamic_cast<const messages::common::PassphraseRequest*>(input.m_msg.get()));
            return true;
        case messages::MessageType_Deprecated_PassphraseStateRequest:
            on_passphrase_state_request(
                    input,
                    dynamic_cast<const messages::common::Deprecated_PassphraseStateRequest*>(
                            input.m_msg.get()));
            return true;
        case messages::MessageType_PinMatrixRequest:
            on_pin_request(
                    input,
                    dynamic_cast<const messages::common::PinMatrixRequest*>(input.m_msg.get()));
            return true;
        default: return false;
    }
}

void device_trezor_base::ensure_derivation_path() noexcept {
    if (m_wallet_deriv_path.empty()) {
        m_wallet_deriv_path.push_back(TREZOR_BIP44_HARDENED_ZERO);  // default 0'
    }
}

void device_trezor_base::set_derivation_path(const std::string& deriv_path) {
    m_wallet_deriv_path.clear();

    if (deriv_path.empty() || deriv_path == "-") {
        ensure_derivation_path();
        return;
    }

    CHECK_AND_ASSERT_THROW_MES(deriv_path.size() <= 255, "Derivation path is too long");

    std::vector<std::string_view> fields = tools::split(deriv_path, "/");
    CHECK_AND_ASSERT_THROW_MES(fields.size() <= 10, "Derivation path is too long");

    m_wallet_deriv_path.reserve(fields.size());
    for (auto& cur : fields) {
        // Required pattern: [0-9]+'? but this is simple enough we can avoid using a regex
        if (!cur.empty() && cur.back() == '\'')
            cur.remove_suffix(1);

        unsigned int cidx;
        bool ok = !cur.empty() && cur.find_first_not_of("0123456789"sv) == std::string_view::npos;
        if (ok)
            ok = tools::parse_int(cur, cidx);
        CHECK_AND_ASSERT_THROW_MES(
                ok, "Invalid wallet code: " << deriv_path << ". Invalid path element: " << cur);

        m_wallet_deriv_path.push_back(cidx | TREZOR_BIP44_HARDENED_ZERO);
    }
}

/* ======================================================================= */
/*                              TREZOR PROTOCOL                            */
/* ======================================================================= */

bool device_trezor_base::ping() {
    auto locks = tools::unique_locks(device_locker, command_locker);
    if (!m_transport) {
        log::info(logcat, "Ping failed, device not connected");
        return false;
    }

    try {
        call_ping_unsafe();
        return true;

    } catch (std::exception const& e) {
        log::error(logcat, "Ping failed, exception thrown {}", e.what());
    } catch (...) {
        log::error(logcat, "Ping failed, general exception thrown");
    }

    return false;
}

void device_trezor_base::device_state_initialize_unsafe() {
    require_connected();
    std::string tmp_session_id;
    auto initMsg = std::make_shared<messages::management::Initialize>();
    OXEN_DEFER {
        memwipe(&tmp_session_id[0], tmp_session_id.size());
    };

    if (!m_device_session_id.empty()) {
        tmp_session_id.assign(m_device_session_id.data(), m_device_session_id.size());
        initMsg->set_allocated_session_id(&tmp_session_id);
    }

    m_features = client_exchange<messages::management::Features>(initMsg);
    if (m_features->has_session_id()) {
        m_device_session_id = m_features->session_id();
    } else {
        m_device_session_id.clear();
    }

    initMsg->release_session_id();
}

void device_trezor_base::device_state_reset() {
    auto locks = tools::unique_locks(device_locker, command_locker);
    device_state_initialize_unsafe();
}

#ifdef WITH_TREZOR_DEBUGGING
#define TREZOR_CALLBACK(method, ...)               \
    do {                                           \
        if (m_debug_callback)                      \
            m_debug_callback->method(__VA_ARGS__); \
        if (m_callback)                            \
            m_callback->method(__VA_ARGS__);       \
    } while (0)
#define TREZOR_CALLBACK_GET(VAR, method, ...)            \
    do {                                                 \
        if (m_debug_callback)                            \
            VAR = m_debug_callback->method(__VA_ARGS__); \
        if (m_callback)                                  \
            VAR = m_callback->method(__VA_ARGS__);       \
    } while (0)

void device_trezor_base::setup_debug() {
    if (!m_debug) {
        return;
    }

    if (!m_debug_callback) {
        CHECK_AND_ASSERT_THROW_MES(m_transport, "Transport does not exist");
        auto debug_transport = m_transport->find_debug();
        if (debug_transport) {
            m_debug_callback = std::make_shared<trezor_debug_callback>(debug_transport);
        } else {
            log::debug(logcat, "Transport does not have debug link option");
        }
    }
}

#else
#define TREZOR_CALLBACK(method, ...)         \
    do {                                     \
        if (m_callback)                      \
            m_callback->method(__VA_ARGS__); \
    } while (0)
#define TREZOR_CALLBACK_GET(VAR, method, ...) \
    VAR = (m_callback ? m_callback->method(__VA_ARGS__) : std::nullopt)
#endif

void device_trezor_base::on_button_request(
        GenericMessage& resp, const messages::common::ButtonRequest* msg) {
    CHECK_AND_ASSERT_THROW_MES(msg, "Empty message");
    log::debug(logcat, "on_button_request, code: {}", msg->code());

    TREZOR_CALLBACK(on_button_request, msg->code());

    messages::common::ButtonAck ack;
    write_raw(&ack);

    resp = read_raw();
}

void device_trezor_base::on_button_pressed() {
    TREZOR_CALLBACK(on_button_pressed);
}

void device_trezor_base::on_pin_request(
        GenericMessage& resp, const messages::common::PinMatrixRequest* msg) {
    log::debug(logcat, "on_pin_request");
    CHECK_AND_ASSERT_THROW_MES(msg, "Empty message");

    std::optional<epee::wipeable_string> pin;
    TREZOR_CALLBACK_GET(pin, on_pin_request);

    if (!pin && m_pin) {
        pin = m_pin;
    }

    std::string pin_field;
    messages::common::PinMatrixAck m;
    if (pin) {
        pin_field.assign(pin->data(), pin->size());
        m.set_allocated_pin(&pin_field);
    }

    OXEN_DEFER {
        m.release_pin();
        if (!pin_field.empty()) {
            memwipe(&pin_field[0], pin_field.size());
        }
    };

    resp = call_raw(&m);
}

void device_trezor_base::on_passphrase_request(
        GenericMessage& resp, const messages::common::PassphraseRequest* msg) {
    CHECK_AND_ASSERT_THROW_MES(msg, "Empty message");
    log::debug(logcat, "on_passhprase_request");

    // Backward compatibility, migration clause.
    if (msg->has__on_device() && msg->_on_device()) {
        messages::common::PassphraseAck m;
        resp = call_raw(&m);
        return;
    }

    bool on_device = true;
    if (msg->has__on_device() && !msg->_on_device()) {
        on_device = false;  // do not enter on device, old devices.
    }

    if (on_device && m_features && m_features->capabilities_size() > 0) {
        on_device = false;
        for (auto it = m_features->capabilities().begin(); it != m_features->capabilities().end();
             it++) {
            if (*it == messages::management::Features::Capability_PassphraseEntry) {
                on_device = true;
            }
        }
    }

    std::optional<epee::wipeable_string> passphrase;
    TREZOR_CALLBACK_GET(passphrase, on_passphrase_request, on_device);

    std::string passphrase_field;
    messages::common::PassphraseAck m;
    m.set_on_device(on_device);
    if (!on_device) {
        if (!passphrase && m_passphrase) {
            passphrase = m_passphrase;
        }

        if (m_passphrase) {
            m_passphrase = std::nullopt;
        }

        if (passphrase) {
            passphrase_field.assign(passphrase->data(), passphrase->size());
            m.set_allocated_passphrase(&passphrase_field);
        }
    }

    OXEN_DEFER {
        m.release_passphrase();
        if (!passphrase_field.empty()) {
            memwipe(&passphrase_field[0], passphrase_field.size());
        }
    };

    resp = call_raw(&m);
}

void device_trezor_base::on_passphrase_state_request(
        GenericMessage& resp, const messages::common::Deprecated_PassphraseStateRequest* msg) {
    log::debug(logcat, "on_passhprase_state_request");
    CHECK_AND_ASSERT_THROW_MES(msg, "Empty message");

    if (msg->has_state()) {
        m_device_session_id = msg->state();
    }
    messages::common::Deprecated_PassphraseStateAck m;
    resp = call_raw(&m);
}

#ifdef WITH_TREZOR_DEBUGGING
void device_trezor_base::wipe_device() {
    auto msg = std::make_shared<messages::management::WipeDevice>();
    auto ret = client_exchange<messages::common::Success>(msg);
    (void)ret;
    init_device();
}

void device_trezor_base::init_device() {
    auto msg = std::make_shared<messages::management::Initialize>();
    m_features = client_exchange<messages::management::Features>(msg);
}

void device_trezor_base::load_device(
        const std::string& mnemonic,
        const std::string& pin,
        bool passphrase_protection,
        const std::string& label,
        const std::string& language,
        bool skip_checksum,
        bool expand) {
    if (m_features && m_features->initialized()) {
        throw oxen::traced<std::runtime_error>(
                "Device is initialized already. Call device.wipe() and try again.");
    }

    auto msg = std::make_shared<messages::management::LoadDevice>();
    msg->add_mnemonics(mnemonic);
    msg->set_pin(pin);
    msg->set_passphrase_protection(passphrase_protection);
    msg->set_label(label);
    msg->set_language(language);
    msg->set_skip_checksum(skip_checksum);
    auto ret = client_exchange<messages::common::Success>(msg);
    (void)ret;

    init_device();
}

trezor_debug_callback::trezor_debug_callback(std::shared_ptr<Transport>& debug_transport) {
    m_debug_link = std::make_shared<DebugLink>();
    m_debug_link->init(debug_transport);
}

void trezor_debug_callback::on_button_request(uint64_t code) {
    if (m_debug_link)
        m_debug_link->press_yes();
}

std::optional<epee::wipeable_string> trezor_debug_callback::on_pin_request() {
    return std::nullopt;
}

std::optional<epee::wipeable_string> trezor_debug_callback::on_passphrase_request(bool& on_device) {
    on_device = true;
    return std::nullopt;
}

void trezor_debug_callback::on_passphrase_state_request(const std::string& state) {}

void trezor_debug_callback::on_disconnect() {
    if (m_debug_link)
        m_debug_link->close();
}
#endif

#endif  // WITH_DEVICE_TREZOR
}  // namespace hw::trezor
