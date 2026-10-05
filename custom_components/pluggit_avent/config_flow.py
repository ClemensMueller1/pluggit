"""Config flow for Pluggit Avent P."""

from __future__ import annotations

from typing import Any

import voluptuous as vol

from homeassistant.config_entries import (
    ConfigEntry,
    ConfigFlow,
    ConfigFlowResult,
    OptionsFlow,
)
from homeassistant.const import CONF_NAME
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers import selector

from .const import (
    CONF_ANALOG_ENTITY,
    CONF_DEVICE_NAME,
    CONF_MODE,
    CONF_MQTT_PREFIX,
    CONF_RF_ADDRESS,
    CONF_RF_PACKET_1,
    CONF_RF_PACKET_2,
    CONF_RF_PACKET_3,
    CONF_RF_PACKET_4,
    CONF_SWITCH_STAGE1,
    CONF_SWITCH_STAGE3,
    DEFAULT_MQTT_PREFIX,
    DEFAULT_NAME,
    DOMAIN,
    MODE_ANALOG,
    MODE_MQTT,
    MODE_RELAY,
    RF_ADDRESS_PUSH_IDS,
    RF_ADDRESS_PUSH_TITLES,
    RF_PACKET_KEYS,
    normalize_rf_address,
    normalize_rf_packet,
)

MODE_SELECTOR = selector.SelectSelector(
    selector.SelectSelectorConfig(
        options=[
            selector.SelectOptionDict(
                value=MODE_RELAY,
                label="Relais / Kontakte an J8 (Shelly, ESPHome)",
            ),
            selector.SelectOptionDict(
                value=MODE_ANALOG,
                label="Analog 0–10 V an J8-7/J8-8",
            ),
            selector.SelectOptionDict(
                value=MODE_MQTT,
                label="MQTT / ESPHome-Gateway (UART oder Funk)",
            ),
        ],
        mode=selector.SelectSelectorMode.LIST,
    )
)

SWITCH_SELECTOR = selector.EntitySelector(
    selector.EntitySelectorConfig(domain=["switch", "input_boolean"])
)

NUMBER_SELECTOR = selector.EntitySelector(
    selector.EntitySelectorConfig(domain=["number", "input_number"])
)

RF_PACKET_SELECTOR = selector.TextSelector(
    selector.TextSelectorConfig(multiline=True)
)


def _user_schema(defaults: dict[str, Any] | None = None) -> vol.Schema:
    defaults = defaults or {}
    return vol.Schema(
        {
            vol.Required(
                CONF_DEVICE_NAME,
                default=defaults.get(CONF_DEVICE_NAME, DEFAULT_NAME),
            ): str,
            vol.Required(
                CONF_MODE, default=defaults.get(CONF_MODE, MODE_RELAY)
            ): MODE_SELECTOR,
        }
    )


def _relay_schema(defaults: dict[str, Any] | None = None) -> vol.Schema:
    defaults = defaults or {}
    schema: dict[Any, Any] = {
        vol.Required(CONF_SWITCH_STAGE1): SWITCH_SELECTOR,
        vol.Required(CONF_SWITCH_STAGE3): SWITCH_SELECTOR,
    }
    if defaults.get(CONF_SWITCH_STAGE1):
        schema = {
            vol.Required(
                CONF_SWITCH_STAGE1, default=defaults[CONF_SWITCH_STAGE1]
            ): SWITCH_SELECTOR,
            vol.Required(
                CONF_SWITCH_STAGE3, default=defaults[CONF_SWITCH_STAGE3]
            ): SWITCH_SELECTOR,
        }
    return vol.Schema(schema)


def _analog_schema(defaults: dict[str, Any] | None = None) -> vol.Schema:
    defaults = defaults or {}
    if defaults.get(CONF_ANALOG_ENTITY):
        return vol.Schema(
            {
                vol.Required(
                    CONF_ANALOG_ENTITY, default=defaults[CONF_ANALOG_ENTITY]
                ): NUMBER_SELECTOR,
            }
        )
    return vol.Schema({vol.Required(CONF_ANALOG_ENTITY): NUMBER_SELECTOR})


def _mqtt_schema(defaults: dict[str, Any] | None = None) -> vol.Schema:
    defaults = defaults or {}
    return vol.Schema(
        {
            vol.Required(
                CONF_MQTT_PREFIX,
                default=defaults.get(CONF_MQTT_PREFIX, DEFAULT_MQTT_PREFIX),
            ): str,
            vol.Optional(
                CONF_RF_ADDRESS, default=defaults.get(CONF_RF_ADDRESS, "")
            ): str,
            vol.Optional(
                CONF_RF_PACKET_1, default=defaults.get(CONF_RF_PACKET_1, "")
            ): RF_PACKET_SELECTOR,
            vol.Optional(
                CONF_RF_PACKET_2, default=defaults.get(CONF_RF_PACKET_2, "")
            ): RF_PACKET_SELECTOR,
            vol.Optional(
                CONF_RF_PACKET_3, default=defaults.get(CONF_RF_PACKET_3, "")
            ): RF_PACKET_SELECTOR,
            vol.Optional(
                CONF_RF_PACKET_4, default=defaults.get(CONF_RF_PACKET_4, "")
            ): RF_PACKET_SELECTOR,
        }
    )


def _apply_mqtt_input(user_input: dict[str, Any]) -> dict[str, Any] | str:
    """Normalize address and the four sniffer payloads. Return an error key."""
    out = dict(user_input)
    try:
        out[CONF_RF_ADDRESS] = normalize_rf_address(user_input.get(CONF_RF_ADDRESS, ""))
    except ValueError:
        return "invalid_rf_address"
    try:
        for key in RF_PACKET_KEYS:
            out[key] = normalize_rf_packet(user_input.get(key, ""))
    except ValueError:
        return "invalid_rf_packet"
    return out


def _mark_rf_address_push(
    hass: HomeAssistant, *, entry_id: str | None = None, title: str | None = None
) -> None:
    """Ask the coordinator to send the saved address to the ESP once."""
    bucket = hass.data.setdefault(DOMAIN, {})
    if entry_id:
        bucket.setdefault(RF_ADDRESS_PUSH_IDS, set()).add(entry_id)
    if title:
        bucket.setdefault(RF_ADDRESS_PUSH_TITLES, set()).add(title)


def _mqtt_available(hass: HomeAssistant) -> bool:
    return "mqtt" in hass.config.components


class PluggitAventConfigFlow(ConfigFlow, domain=DOMAIN):
    """Handle the initial setup."""

    VERSION = 1

    def __init__(self) -> None:
        self._data: dict[str, Any] = {}

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        if user_input is None:
            return self.async_show_form(step_id="user", data_schema=_user_schema())

        self._data[CONF_DEVICE_NAME] = user_input[CONF_DEVICE_NAME]
        self._data[CONF_MODE] = user_input[CONF_MODE]
        self._data[CONF_NAME] = user_input[CONF_DEVICE_NAME]

        await self.async_set_unique_id(
            f"{DOMAIN}_{user_input[CONF_MODE]}_{user_input[CONF_DEVICE_NAME].lower().replace(' ', '_')}"
        )
        self._abort_if_unique_id_configured()

        if user_input[CONF_MODE] == MODE_RELAY:
            return await self.async_step_relay()
        if user_input[CONF_MODE] == MODE_ANALOG:
            return await self.async_step_analog()
        return await self.async_step_mqtt()

    async def async_step_relay(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            if user_input[CONF_SWITCH_STAGE1] == user_input[CONF_SWITCH_STAGE3]:
                errors["base"] = "same_switches"
            else:
                self._data.update(user_input)
                return self.async_create_entry(
                    title=self._data[CONF_DEVICE_NAME], data=self._data
                )
        return self.async_show_form(
            step_id="relay", data_schema=_relay_schema(), errors=errors
        )

    async def async_step_analog(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        if user_input is not None:
            self._data.update(user_input)
            return self.async_create_entry(
                title=self._data[CONF_DEVICE_NAME], data=self._data
            )
        return self.async_show_form(step_id="analog", data_schema=_analog_schema())

    async def async_step_mqtt(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            if not _mqtt_available(self.hass):
                errors["base"] = "mqtt_not_ready"
            else:
                parsed = _apply_mqtt_input(user_input)
                if isinstance(parsed, str):
                    errors["base"] = parsed
                else:
                    parsed[CONF_MQTT_PREFIX] = parsed[CONF_MQTT_PREFIX].strip().strip("/")
                    self._data.update(parsed)
                    if parsed.get(CONF_RF_ADDRESS):
                        _mark_rf_address_push(
                            self.hass, title=self._data[CONF_DEVICE_NAME]
                        )
                    return self.async_create_entry(
                        title=self._data[CONF_DEVICE_NAME], data=self._data
                    )
        return self.async_show_form(
            step_id="mqtt",
            data_schema=_mqtt_schema(),
            errors=errors,
        )

    @staticmethod
    @callback
    def async_get_options_flow(config_entry: ConfigEntry) -> OptionsFlow:
        return PluggitAventOptionsFlow()


class PluggitAventOptionsFlow(OptionsFlow):
    """Change wiring / MQTT prefix after setup."""

    async def async_step_init(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        mode = self.config_entry.data[CONF_MODE]
        current = {**self.config_entry.data, **self.config_entry.options}

        if user_input is not None:
            if (
                mode == MODE_RELAY
                and user_input.get(CONF_SWITCH_STAGE1)
                == user_input.get(CONF_SWITCH_STAGE3)
            ):
                return self.async_show_form(
                    step_id="init",
                    data_schema=self._schema(mode, current),
                    errors={"base": "same_switches"},
                )
            if mode == MODE_MQTT:
                parsed = _apply_mqtt_input(user_input)
                if isinstance(parsed, str):
                    return self.async_show_form(
                        step_id="init",
                        data_schema=self._schema(mode, current),
                        errors={"base": parsed},
                    )
                parsed[CONF_MQTT_PREFIX] = parsed[CONF_MQTT_PREFIX].strip().strip("/")
                if parsed.get(CONF_RF_ADDRESS):
                    _mark_rf_address_push(
                        self.hass, entry_id=self.config_entry.entry_id
                    )
                user_input = parsed
            return self.async_create_entry(title="", data=user_input)

        return self.async_show_form(
            step_id="init", data_schema=self._schema(mode, current)
        )

    def _schema(self, mode: str, current: dict[str, Any]) -> vol.Schema:
        fields: dict[Any, Any] = {
            vol.Required(
                CONF_DEVICE_NAME,
                default=current.get(CONF_DEVICE_NAME, DEFAULT_NAME),
            ): str,
        }
        if mode == MODE_RELAY:
            fields[vol.Required(CONF_SWITCH_STAGE1, default=current.get(CONF_SWITCH_STAGE1))] = (
                SWITCH_SELECTOR
            )
            fields[vol.Required(CONF_SWITCH_STAGE3, default=current.get(CONF_SWITCH_STAGE3))] = (
                SWITCH_SELECTOR
            )
        elif mode == MODE_ANALOG:
            fields[
                vol.Required(CONF_ANALOG_ENTITY, default=current.get(CONF_ANALOG_ENTITY))
            ] = NUMBER_SELECTOR
        else:
            fields[
                vol.Required(
                    CONF_MQTT_PREFIX,
                    default=current.get(CONF_MQTT_PREFIX, DEFAULT_MQTT_PREFIX),
                )
            ] = str
            fields[
                vol.Optional(
                    CONF_RF_ADDRESS, default=current.get(CONF_RF_ADDRESS, "")
                )
            ] = str
            fields[
                vol.Optional(
                    CONF_RF_PACKET_1, default=current.get(CONF_RF_PACKET_1, "")
                )
            ] = RF_PACKET_SELECTOR
            fields[
                vol.Optional(
                    CONF_RF_PACKET_2, default=current.get(CONF_RF_PACKET_2, "")
                )
            ] = RF_PACKET_SELECTOR
            fields[
                vol.Optional(
                    CONF_RF_PACKET_3, default=current.get(CONF_RF_PACKET_3, "")
                )
            ] = RF_PACKET_SELECTOR
            fields[
                vol.Optional(
                    CONF_RF_PACKET_4, default=current.get(CONF_RF_PACKET_4, "")
                )
            ] = RF_PACKET_SELECTOR
        return vol.Schema(fields)
