"""nRF905 868.4 MHz sniffer / replay for Pluggit DTH-029255-04."""

from esphome import automation, pins
import esphome.codegen as cg
from esphome.components import binary_sensor, spi, text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TRIGGER_ID, DEVICE_CLASS_CONNECTIVITY

CODEOWNERS = ["@ClemensMueller1"]
DEPENDENCIES = ["spi"]
AUTO_LOAD = ["text_sensor", "binary_sensor"]

CONF_CE_PIN = "ce_pin"
CONF_TXE_PIN = "txe_pin"
CONF_PWR_PIN = "pwr_pin"
CONF_DR_PIN = "dr_pin"
CONF_LAST_PACKET = "last_packet"
CONF_RF_RX = "rf_rx"
CONF_ON_PACKET = "on_packet"
CONF_DATA = "data"

nrf905_pluggit_ns = cg.esphome_ns.namespace("nrf905_pluggit")
Nrf905Pluggit = nrf905_pluggit_ns.class_("Nrf905Pluggit", cg.Component, spi.SPIDevice)
Nrf905PacketTrigger = nrf905_pluggit_ns.class_(
    "Nrf905PacketTrigger", automation.Trigger.template(cg.std_string)
)
Nrf905TransmitAction = nrf905_pluggit_ns.class_("Nrf905TransmitAction", automation.Action)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Nrf905Pluggit),
            cv.Required(CONF_CE_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_TXE_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_PWR_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_DR_PIN): pins.gpio_input_pin_schema,
            cv.Optional(CONF_LAST_PACKET): text_sensor.text_sensor_schema(
                icon="mdi:radio-tower",
            ),
            cv.Optional(CONF_RF_RX): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
            ),
            cv.Optional(CONF_ON_PACKET): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(Nrf905PacketTrigger),
                }
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=True))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)

    cg.add(var.set_ce_pin(await cg.gpio_pin_expression(config[CONF_CE_PIN])))
    cg.add(var.set_txe_pin(await cg.gpio_pin_expression(config[CONF_TXE_PIN])))
    cg.add(var.set_pwr_pin(await cg.gpio_pin_expression(config[CONF_PWR_PIN])))
    cg.add(var.set_dr_pin(await cg.gpio_pin_expression(config[CONF_DR_PIN])))

    if CONF_LAST_PACKET in config:
        sens = await text_sensor.new_text_sensor(config[CONF_LAST_PACKET])
        cg.add(var.set_last_packet(sens))

    if CONF_RF_RX in config:
        bin_sens = await binary_sensor.new_binary_sensor(config[CONF_RF_RX])
        cg.add(var.set_rf_rx(bin_sens))

    for conf in config.get(CONF_ON_PACKET, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.register_on_packet_trigger(trigger))
        await automation.build_automation(trigger, [(cg.std_string, "x")], conf)


@automation.register_action(
    "nrf905_pluggit.transmit",
    Nrf905TransmitAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(Nrf905Pluggit),
            cv.Required(CONF_DATA): cv.templatable(cv.string),
        }
    ),
    synchronous=True,
)
async def nrf905_transmit_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    template_ = await cg.templatable(config[CONF_DATA], args, cg.std_string)
    cg.add(var.set_data(template_))
    return var
