import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c, sensor, binary_sensor, text_sensor, esp32, motion
from esphome.const import CONF_ID

DEPENDENCIES = ["i2c", "esp32"]
AUTO_LOAD = ["sensor", "binary_sensor", "text_sensor"]
ns = cg.esphome_ns.namespace("badge_hardware")
BadgeHardware = ns.class_("BadgeHardware", cg.PollingComponent, i2c.I2CDevice)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(BadgeHardware),
    cv.Required("motion_id"): cv.use_id(motion.MotionComponent),
    cv.Required("temperature"): sensor.sensor_schema(unit_of_measurement="°C", accuracy_decimals=2, device_class="temperature", state_class="measurement"),
    cv.Required("humidity"): sensor.sensor_schema(unit_of_measurement="%", accuracy_decimals=1, device_class="humidity", state_class="measurement"),
    cv.Required("pressure"): sensor.sensor_schema(unit_of_measurement="hPa", accuracy_decimals=1, device_class="atmospheric_pressure", state_class="measurement"),
    cv.Required("gas_resistance"): sensor.sensor_schema(unit_of_measurement="Ω", accuracy_decimals=0, state_class="measurement"),
    cv.Required("gas_response"): sensor.sensor_schema(unit_of_measurement="%", accuracy_decimals=1, state_class="measurement"),
    cv.Required("gas_level"): text_sensor.text_sensor_schema(),
    cv.Required("gas_trend"): text_sensor.text_sensor_schema(),
    cv.Required("sd_mounted"): binary_sensor.binary_sensor_schema(),
    cv.Required("sd_status"): text_sensor.text_sensor_schema(),
    cv.Required("sd_capacity"): sensor.sensor_schema(unit_of_measurement="MB", accuracy_decimals=0),
}).extend(cv.polling_component_schema("10s")).extend(i2c.i2c_device_schema(0x76))

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.set_motion(await cg.get_variable(config["motion_id"])))
    for key in ("temperature", "humidity", "pressure", "gas_resistance", "gas_response", "sd_capacity"):
        obj = await sensor.new_sensor(config[key])
        cg.add(getattr(var, "set_" + key)(obj))
    cg.add(var.set_sd_mounted(await binary_sensor.new_binary_sensor(config["sd_mounted"])))
    cg.add(var.set_sd_status(await text_sensor.new_text_sensor(config["sd_status"])))
    cg.add(var.set_gas_level(await text_sensor.new_text_sensor(config["gas_level"])))
    cg.add(var.set_gas_trend(await text_sensor.new_text_sensor(config["gas_trend"])))
    for name in ("fatfs", "sdmmc", "esp_driver_sdmmc", "sd_pwr_ctrl"):
        esp32.include_builtin_idf_component(name)
