"""Constants for the Pluggit Avent P integration."""

from __future__ import annotations

from enum import IntEnum
from typing import Final

DOMAIN: Final = "pluggit_avent"
VERSION: Final = "1.1.2"
RELEASE_TAG: Final = f"v{VERSION}"
REPO_URL: Final = "https://github.com/ClemensMueller1/pluggit"
CHANGELOG_URL: Final = f"{REPO_URL}/blob/{RELEASE_TAG}/CHANGELOG.md"
RELEASE_URL: Final = f"{REPO_URL}/releases/tag/{RELEASE_TAG}"

CONF_MODE: Final = "mode"
CONF_SWITCH_STAGE1: Final = "switch_stage1"
CONF_SWITCH_STAGE3: Final = "switch_stage3"
CONF_ANALOG_ENTITY: Final = "analog_entity"
CONF_MQTT_PREFIX: Final = "mqtt_prefix"
CONF_DEVICE_NAME: Final = "device_name"
CONF_RF_PACKET_1: Final = "rf_packet_1"
CONF_RF_PACKET_2: Final = "rf_packet_2"
CONF_RF_PACKET_3: Final = "rf_packet_3"
CONF_RF_PACKET_4: Final = "rf_packet_4"
CONF_RF_ADDRESS: Final = "rf_address"

MODE_RELAY: Final = "relay"
MODE_ANALOG: Final = "analog"
MODE_MQTT: Final = "mqtt"

PRESET_STAGE1: Final = "Stufe 1"
PRESET_STAGE2: Final = "Stufe 2"
PRESET_STAGE3: Final = "Stufe 3"
PRESET_REMOTE: Final = "Fernbedienung"
PRESET_OFF: Final = "Aus"

PRESET_MODES_RELAY: Final = (PRESET_STAGE1, PRESET_STAGE2, PRESET_STAGE3)
PRESET_MODES_ANALOG: Final = (PRESET_REMOTE, PRESET_STAGE1, PRESET_STAGE2, PRESET_STAGE3)
PRESET_MODES_MQTT: Final = (PRESET_OFF, PRESET_STAGE1, PRESET_STAGE2, PRESET_STAGE3)

RF_PACKET_LEN: Final = 32
RF_ADDRESS_LEN: Final = 4
RF_PACKET_KEYS: Final = (
    CONF_RF_PACKET_1,
    CONF_RF_PACKET_2,
    CONF_RF_PACKET_3,
    CONF_RF_PACKET_4,
)

# Official analog control voltages on J8-7 / J8-8 (potential-free).
VOLTAGE_REMOTE: Final = 0.0
VOLTAGE_STAGE1: Final = 3.0
VOLTAGE_STAGE2: Final = 6.0
VOLTAGE_STAGE3: Final = 9.0

VOLTAGE_TOLERANCE: Final = 0.8

MQTT_CMD_SPEED: Final = "speed/set"
MQTT_CMD_RF_TX: Final = "rf/tx"
MQTT_CMD_RF_ADDRESS: Final = "rf/address/set"
MQTT_STATE_RF_ADDRESS: Final = "rf/address"
MQTT_STATE_RF_RX: Final = "rf/rx"
MQTT_STATE_SPEED: Final = "speed"
MQTT_STATE_T1: Final = "t1"
MQTT_STATE_T2: Final = "t2"
MQTT_STATE_T3: Final = "t3"
MQTT_STATE_T4: Final = "t4"
MQTT_STATE_RH: Final = "humidity"
MQTT_STATE_FILTER: Final = "filter_alarm"
MQTT_STATE_FAULT: Final = "fault"
MQTT_STATE_BYPASS: Final = "bypass"
MQTT_AVAILABILITY: Final = "availability"

DEFAULT_MQTT_PREFIX: Final = "pluggit_avent"
DEFAULT_NAME: Final = "Pluggit Avent P"


def device_info(entry_id: str, name: str) -> dict:
    """Device registry entry. Links the integration version to the release."""
    return {
        "identifiers": {(DOMAIN, entry_id)},
        "name": name,
        "manufacturer": "Pluggit",
        "model": "Avent P / P300 / P450",
        "sw_version": VERSION,
        "configuration_url": RELEASE_URL,
    }


# Set by the config/options flow when the user saved a ShockBurst address.
# The coordinator consumes the marker once and publishes rf/address/set.
RF_ADDRESS_PUSH_IDS: Final = "_rf_address_push_ids"
RF_ADDRESS_PUSH_TITLES: Final = "_rf_address_push_titles"

INTERLOCK_DELAY_S: Final = 0.25


class FanStage(IntEnum):
    """Ventilation stages of the Avent P / P300 / P450."""

    REMOTE = 0
    STAGE1 = 1
    STAGE2 = 2
    STAGE3 = 3


STAGE_TO_PRESET: Final = {
    FanStage.REMOTE: PRESET_REMOTE,
    FanStage.STAGE1: PRESET_STAGE1,
    FanStage.STAGE2: PRESET_STAGE2,
    FanStage.STAGE3: PRESET_STAGE3,
}

PRESET_TO_STAGE: Final = {
    PRESET_REMOTE: FanStage.REMOTE,
    PRESET_OFF: FanStage.REMOTE,
    PRESET_STAGE1: FanStage.STAGE1,
    PRESET_STAGE2: FanStage.STAGE2,
    PRESET_STAGE3: FanStage.STAGE3,
}

STAGE_TO_RF_PACKET: Final = {
    FanStage.REMOTE: CONF_RF_PACKET_1,
    FanStage.STAGE1: CONF_RF_PACKET_2,
    FanStage.STAGE2: CONF_RF_PACKET_3,
    FanStage.STAGE3: CONF_RF_PACKET_4,
}

STAGE_TO_VOLTAGE: Final = {
    FanStage.REMOTE: VOLTAGE_REMOTE,
    FanStage.STAGE1: VOLTAGE_STAGE1,
    FanStage.STAGE2: VOLTAGE_STAGE2,
    FanStage.STAGE3: VOLTAGE_STAGE3,
}

STAGE_TO_PERCENTAGE: Final = {
    FanStage.REMOTE: 0,
    FanStage.STAGE1: 33,
    FanStage.STAGE2: 66,
    FanStage.STAGE3: 100,
}


def normalize_rf_address(value: str | None) -> str:
    """Return spaced uppercase hex for a 4-byte ShockBurst address, or empty."""
    if not value:
        return ""
    hex_chars = "".join(c for c in value if c in "0123456789abcdefABCDEF")
    if not hex_chars:
        return ""
    if len(hex_chars) != RF_ADDRESS_LEN * 2:
        raise ValueError(
            f"ShockBurst address must be {RF_ADDRESS_LEN} bytes "
            f"({RF_ADDRESS_LEN * 2} hex digits)"
        )
    normalized = " ".join(
        hex_chars[i : i + 2].upper() for i in range(0, len(hex_chars), 2)
    )
    if normalized == "00 00 00 00":
        raise ValueError("ShockBurst address must not be all zeros")
    return normalized


def normalize_rf_packet(value: str | None) -> str:
    """Return spaced uppercase hex for a 32-byte nRF905 payload, or empty."""
    if not value:
        return ""
    hex_chars = "".join(c for c in value if c in "0123456789abcdefABCDEF")
    if not hex_chars:
        return ""
    if len(hex_chars) != RF_PACKET_LEN * 2:
        raise ValueError(
            f"RF packet must be {RF_PACKET_LEN} bytes ({RF_PACKET_LEN * 2} hex digits)"
        )
    return " ".join(hex_chars[i : i + 2].upper() for i in range(0, len(hex_chars), 2))


def percentage_to_stage(percentage: int | None, *, allow_remote: bool) -> FanStage:
    """Map a 0-100 percentage onto a discrete fan stage."""
    if percentage is None:
        return FanStage.STAGE2
    if allow_remote and percentage < 17:
        return FanStage.REMOTE
    if percentage <= 50:
        return FanStage.STAGE1
    if percentage <= 83:
        return FanStage.STAGE2
    return FanStage.STAGE3


def voltage_to_stage(voltage: float | None) -> FanStage:
    """Map a measured analog voltage onto a fan stage."""
    if voltage is None:
        return FanStage.STAGE2
    if voltage < VOLTAGE_STAGE1 - VOLTAGE_TOLERANCE:
        return FanStage.REMOTE
    if abs(voltage - VOLTAGE_STAGE1) <= VOLTAGE_TOLERANCE:
        return FanStage.STAGE1
    if abs(voltage - VOLTAGE_STAGE2) <= VOLTAGE_TOLERANCE:
        return FanStage.STAGE2
    if abs(voltage - VOLTAGE_STAGE3) <= VOLTAGE_TOLERANCE:
        return FanStage.STAGE3
    if voltage < 4.5:
        return FanStage.STAGE1
    if voltage < 7.5:
        return FanStage.STAGE2
    return FanStage.STAGE3
