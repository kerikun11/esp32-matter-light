/**
 * CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME
 *
 * Human readable vendor name for the organization responsible for producing the device.
 */
// #define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME "TEST_VENDOR"
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME "KERI's Lab"

/**
 * CHIP_DEVICE_CONFIG_DEVICE_VENDOR_ID
 *
 * The CHIP-assigned vendor id for the organization responsible for producing the device.
 *
 * Default is the Test VendorID of 0xFFF1.
 *
 * Un-overridden default must match the default test DAC
 * (see src/credentials/examples/DeviceAttestationCredsExample.cpp).
 */
// #define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_ID 0xFFF1
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_ID 0xFFF2

/**
 * CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME
 *
 * Human readable name of the device model.
 */
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME "KERI's Lab Matter Device"

/**
 * CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID
 *
 * The unique id assigned by the device vendor to identify the product or device type.  This
 * number is scoped to the device vendor id.
 *
 * Un-overridden default must either match one of the given development certs
 * or have a DeviceAttestationCredentialsProvider implemented.
 * (see src/credentials/examples/DeviceAttestationCredsExample.cpp)
 */
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID 0x8001

/**
 * CHIP_DEVICE_CONFIG_DEFAULT_DEVICE_HARDWARE_VERSION_STRING
 *
 * Human readable string identifying version of the product assigned by the device vendor.
 */
#define CHIP_DEVICE_CONFIG_DEFAULT_DEVICE_HARDWARE_VERSION_STRING "0.0.0"

/**
 * CHIP_DEVICE_CONFIG_DEFAULT_DEVICE_HARDWARE_VERSION
 *
 * The default hardware version number assigned to the device or product by the device vendor.
 *
 * Hardware versions are specific to a particular device vendor and product id, and typically
 * correspond to a revision of the physical device, a change to its packaging, and/or a change
 * to its marketing presentation. This value is generally *not* incremented for device software
 * revisions.
 *
 * This is a default value which is used when a hardware version has not been stored in device
 * persistent storage (e.g. by a factory provisioning process).
 */
#define CHIP_DEVICE_CONFIG_DEFAULT_DEVICE_HARDWARE_VERSION 0

/**
 * CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING
 *
 * A string identifying the software version running on the device.
 */
#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING "0.0.0"

/**
 * CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION
 *
 * A monothonic number identifying the software version running on the device.
 */
#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION 0
