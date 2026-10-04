import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c, number, rtttl
from esphome.components.badge_hardware import BadgeHardware
from esphome.const import CONF_ID

AUTO_LOAD = ["json"]

DEPENDENCIES = ["i2c", "lvgl", "wifi", "api", "number", "badge_hardware", "rtttl"]
ns = cg.esphome_ns.namespace("badge_keyboard")
BadgeKeyboard = ns.class_("BadgeKeyboard", cg.PollingComponent, i2c.I2CDevice)
CONFIG_SCHEMA = cv.Schema({cv.GenerateID(): cv.declare_id(BadgeKeyboard),
    cv.Required("hardware_id"): cv.use_id(BadgeHardware),
    cv.Required("audio_id"): cv.use_id(rtttl.Rtttl),
    cv.Required("brightness_number"): cv.use_id(number.Number),
    cv.Required("keyboard_brightness_number"): cv.use_id(number.Number)}).extend(
    cv.polling_component_schema("10ms")
).extend(i2c.i2c_device_schema(0x34))


async def to_code(config):
    from esphome.components.lvgl.defines import add_lv_use
    add_lv_use("textarea")
    add_lv_use("label")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    screen_number = await cg.get_variable(config["brightness_number"])
    cg.add(var.set_brightness_number(screen_number))
    keyboard_number = await cg.get_variable(config["keyboard_brightness_number"])
    cg.add(var.set_keyboard_brightness_number(keyboard_number))
    cg.add(var.set_hardware(await cg.get_variable(config["hardware_id"])))
    cg.add(var.set_audio(await cg.get_variable(config["audio_id"])))
