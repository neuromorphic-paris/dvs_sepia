#pragma once

#include "../third_party/sepia/source/sepia.hpp"
#include <array>
#include <libusb-1.0/libusb.h>

/// dvs_sepia specialises sepia for the Dynamic Vision Sensor 128.
/// In order to use this header, an application must link to the dynamic library usb-1.0.
namespace dvs_sepia {

    /// camera represents a DVS128.
    class camera {
        public:
        /// available_serials returns the connected DVS cameras' serials.
        static std::vector<std::string> available_serials() {
            std::vector<std::string> serials;
            libusb_context* context;
            check_usb_error(libusb_init(&context), "initializing the USB context");
            libusb_device** devices;
            const auto count = libusb_get_device_list(context, &devices);
            for (std::size_t index = 0; index < count; ++index) {
                libusb_device_descriptor descriptor;
                if (libusb_get_device_descriptor(devices[index], &descriptor) == 0) {
                    if (descriptor.idVendor == 5418 && descriptor.idProduct == 33792) {
                        libusb_device_handle* handle;
                        check_usb_error(libusb_open(devices[index], &handle), "opening the device");
                        if (libusb_claim_interface(handle, 0) == 0) {
                            std::array<uint8_t, 8> bytes;
                            auto bytes_read = libusb_get_string_descriptor_ascii(handle, 3, bytes.data(), bytes.size());
                            if (bytes_read <= 0) {
                                throw std::logic_error("retrieving the serial failed");
                            }
                            serials.push_back(std::string(bytes.data(), bytes.data() + bytes_read));
                        }
                        libusb_close(handle);
                    }
                }
            }
            libusb_free_device_list(devices, 1);
            libusb_exit(context);
            return serials;
        }

        /// default_parameter returns the default parameter used by the DVS.
        static std::unique_ptr<sepia::parameter> default_parameter() {
            return sepia::make_unique<sepia::object_parameter>(
                "is_timestamp_master",
                sepia::make_unique<sepia::boolean_parameter>(true),
                "first_stage_amplifier_cascode",
                sepia::make_unique<sepia::number_parameter>(1992, 0, 1 << 24, true),
                "injected_ground",
                sepia::make_unique<sepia::number_parameter>(1108364, 0, 1 << 24, true),
                "chip_request_pull_down",
                sepia::make_unique<sepia::number_parameter>(16777215, 0, 1 << 24, true),
                "x_arbiter_request_pull_up",
                sepia::make_unique<sepia::number_parameter>(8159221, 0, 1 << 24, true),
                "off_event_threshold",
                sepia::make_unique<sepia::number_parameter>(132, 0, 1 << 24, true),
                "passive_load_pull_down",
                sepia::make_unique<sepia::number_parameter>(309590, 0, 1 << 24, true),
                "refractory_period",
                sepia::make_unique<sepia::number_parameter>(969, 0, 1 << 24, true),
                "y_arbiter_request_pull_up",
                sepia::make_unique<sepia::number_parameter>(16777215, 0, 1 << 24, true),
                "on_event_threshold",
                sepia::make_unique<sepia::number_parameter>(209996, 0, 1 << 24, true),
                "second_stage_amplifier",
                sepia::make_unique<sepia::number_parameter>(13125, 0, 1 << 24, true),
                "source_follower",
                sepia::make_unique<sepia::number_parameter>(271, 0, 1 << 24, true),
                "photoreceptor",
                sepia::make_unique<sepia::number_parameter>(217, 0, 1 << 24, true));
        }

        /// width returns the sensor width.
        static constexpr uint16_t width() {
            return 128;
        }

        /// height returns the sensor height.
        static constexpr uint16_t height() {
            return 128;
        }

        /// name_to_address contains the settings for the digital-to-analog converters on the chip.
        static std::unordered_map<std::string, uint8_t> name_to_address() {
            return {
                {"first_stage_amplifier_cascode", 0x00},
                {"injected_ground", 0x01},
                {"chip_request_pull_down", 0x02},
                {"x_arbiter_request_pull_up", 0x03},
                {"off_event_threshold", 0x04},
                {"passive_load_pull_down", 0x05},
                {"refractory_period", 0x06},
                {"y_arbiter_request_pull_up", 0x07},
                {"on_event_threshold", 0x08},
                {"second_stage_amplifier", 0x09},
                {"source_follower", 0x0a},
                {"photoreceptor", 0x0b},
            };
        };

        camera() = default;
        camera(const camera&) = delete;
        camera(camera&&) = default;
        camera& operator=(const camera&) = delete;
        camera& operator=(camera&&) = default;
        virtual ~camera() {}

        protected:
        /// check_usb_error throws if the given value is not zero.
        static void check_usb_error(int error, const std::string& message) {
            if (error < 0) {
                throw std::logic_error(message + " failed: " + libusb_strerror(static_cast<libusb_error>(error)));
            }
        }
    };

    /// specialized_camera represents a template-specialized DVS128.
    template <typename HandleEvent, typename HandleException>
    class specialized_camera : public camera,
                               public sepia::specialized_camera<sepia::dvs_event, HandleEvent, HandleException> {
        public:
        specialized_camera(
            HandleEvent handle_event,
            HandleException handle_exception,
            std::unique_ptr<sepia::unvalidated_parameter> unvalidated_parameter,
            std::size_t fifo_size,
            const std::string& serial,
            std::chrono::milliseconds sleep_duration) :
            sepia::specialized_camera<sepia::dvs_event, HandleEvent, HandleException>(
                std::forward<HandleEvent>(handle_event),
                std::forward<HandleException>(handle_exception),
                fifo_size,
                sleep_duration),
            _parameter(default_parameter()),
            _acquisition_running(true) {
            _parameter->parse_or_load(std::move(unvalidated_parameter));

            // initialize the context
            check_usb_error(libusb_init(&_context), "initializing the USB context");

            // find requested / available devices
            {
                auto device_found = false;
                libusb_device** devices;
                const auto count = libusb_get_device_list(_context, &devices);
                for (std::size_t index = 0; index < count; ++index) {
                    libusb_device_descriptor descriptor;
                    if (libusb_get_device_descriptor(devices[index], &descriptor) == 0) {
                        if (descriptor.idVendor == 5418 && descriptor.idProduct == 33792) {
                            check_usb_error(libusb_open(devices[index], &_handle), "opening the device");
                            if (libusb_claim_interface(_handle, 0) == 0) {
                                if (serial.empty()) {
                                    device_found = true;
                                    break;
                                } else {
                                    std::array<uint8_t, 8> bytes;
                                    auto bytes_read =
                                        libusb_get_string_descriptor_ascii(_handle, 3, bytes.data(), bytes.size());
                                    if (bytes_read <= 0) {
                                        throw std::logic_error("retrieving the serial failed");
                                    }
                                    if (std::string(bytes.data(), bytes.data() + bytes_read) == serial) {
                                        device_found = true;
                                        break;
                                    }
                                }
                                libusb_release_interface(_handle, 0);
                            }
                            libusb_close(_handle);
                        }
                    }
                }
                libusb_free_device_list(devices, 1);
                if (!device_found) {
                    libusb_exit(_context);
                    throw sepia::no_device_connected("DVS");
                }
            }

            // allocate a transfer
            _transfer = libusb_alloc_transfer(0);

            // send setup commands to the camera
            check_usb_error(libusb_reset_device(_handle), "resetting the device");
            std::array<uint8_t, 12 * 3> biases;
            for (const auto& name_and_address : name_to_address()) {
                const auto value = static_cast<uint32_t>(_parameter->get_number({name_and_address.first}));
                biases[name_and_address.second * 3] = static_cast<uint8_t>((value >> 16) & 0xff);
                biases[name_and_address.second * 3 + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
                biases[name_and_address.second * 3 + 2] = static_cast<uint8_t>((value >> 8) & 0xff);
            }
            check_usb_error(
                libusb_control_transfer(_handle, 64, 184, 0, 0, biases.data(), biases.size(), 0),
                "sending a control packet");
            check_usb_error(libusb_control_transfer(_handle, 64, 179, 0, 0, nullptr, 0, 0), "starting the data flow");

            // start the reading loop
            _acquisition_loop = std::thread([this, serial]() -> void {
                try {
                    std::vector<uint8_t> bytes(4096);
                    sepia::dvs_event event;
                    uint64_t t_offset = 0;
                    while (_acquisition_running.load(std::memory_order_relaxed)) {
                        int32_t transferred = 0;
                        const auto error = libusb_bulk_transfer(
                            _handle, 134, bytes.data(), static_cast<uint32_t>(bytes.size()), &transferred, 0);
                        if ((error == 0 || error == LIBUSB_ERROR_TIMEOUT) && transferred % 4 == 0) {
                            for (auto byte_iterator = bytes.begin();
                                 byte_iterator != std::next(bytes.begin(), transferred);
                                 std::advance(byte_iterator, 4)) {
                                if ((*std::next(byte_iterator, 3) & 0x80) == 0x80) {
                                    t_offset += 0x4000;
                                } else if ((*std::next(byte_iterator, 3) & 0x40) == 0x40) {
                                    t_offset = 0;
                                } else if ((*std::next(byte_iterator, 1) & 0x80) == 0) {
                                    event.t = (*std::next(byte_iterator, 2)
                                               | (static_cast<uint64_t>(*std::next(byte_iterator, 3)) << 8))
                                              + t_offset;
                                    event.x = 128 - (*byte_iterator >> 1);
                                    event.y = (*std::next(byte_iterator, 1) & 0x7f);
                                    event.is_increase = (*byte_iterator & 1) == 0;
                                    if (!this->push(event)) {
                                        throw std::runtime_error("computer's FIFO overflow");
                                    }
                                }
                            }
                        } else {
                            throw sepia::device_disconnected("DVS");
                        }
                    }
                } catch (...) {
                    this->_handle_exception(std::current_exception());
                }
            });
        }
        specialized_camera(const specialized_camera&) = delete;
        specialized_camera(specialized_camera&&) = default;
        specialized_camera& operator=(const specialized_camera&) = delete;
        specialized_camera& operator=(specialized_camera&&) = default;
        virtual ~specialized_camera() {
            _acquisition_running.store(false, std::memory_order_relaxed);
            _acquisition_loop.join();
            libusb_release_interface(_handle, 0);
            libusb_free_transfer(_transfer);
            libusb_close(_handle);
            libusb_exit(_context);
        }

        protected:
        std::unique_ptr<sepia::parameter> _parameter;
        std::atomic_bool _acquisition_running;
        libusb_context* _context;
        libusb_device_handle* _handle;
        libusb_transfer* _transfer;
        std::thread _acquisition_loop;
    };

    /// make_camera creates a camera from functors.
    template <typename HandleEvent, typename HandleException>
    std::unique_ptr<specialized_camera<HandleEvent, HandleException>> make_camera(
        HandleEvent handle_event,
        HandleException handle_exception,
        std::unique_ptr<sepia::unvalidated_parameter> unvalidated_parameter =
            std::unique_ptr<sepia::unvalidated_parameter>(),
        std::size_t fifo_size = 1 << 24,
        const std::string& serial = std::string(),
        std::chrono::milliseconds sleep_duration = std::chrono::milliseconds(10)) {
        return sepia::make_unique<specialized_camera<HandleEvent, HandleException>>(
            std::forward<HandleEvent>(handle_event),
            std::forward<HandleException>(handle_exception),
            std::move(unvalidated_parameter),
            fifo_size,
            serial,
            sleep_duration);
    }
}
