// Wi-Fi "ports" of the boards that have Wi-Fi (ESP32, ESP8266): the Live mode reaches them over the
// network instead of the USB cable. The C++ host recognises the "wifi:" prefix
// (core/include/s2a/network.hpp).
import type { Port } from "@/bridge";
import { findBoard } from "@/catalog";

export const WIFI_PREFIX = "wifi:";
/** The board on its own network "S2A-xxxx" (password below) always has this address. */
export const ACCESS_POINT_ADDRESS = "192.168.4.1";
export const ACCESS_POINT_PASSWORD = "scratch2arduino";
const STORAGE_KEY = "s2a.wifi.addresses";
const REMEMBERED = 3;

/** Boards whose Live firmware has Wi-Fi ("wifi" in resources/boards/<id>.json). */
export const hasWifi = (boardId: string): boolean => findBoard(boardId)?.wifi ?? false;
export const isWifiPort = (port: string | null): boolean => port?.startsWith(WIFI_PREFIX) ?? false;
export const wifiPort = (address: string): string => `${WIFI_PREFIX}${address}`;

/** Addresses of boards found on the home network, most recent first (kept in this browser only). */
export function rememberedAddresses(): string[] {
	try {
		const stored: unknown = JSON.parse(localStorage.getItem(STORAGE_KEY) ?? "[]");
		return Array.isArray(stored) ? stored.filter((item): item is string => typeof item === "string") : [];
	} catch {
		return [];
	}
}

export function rememberAddress(address: string): string[] {
	const addresses = [address, ...rememberedAddresses().filter((known) => known !== address)].slice(0, REMEMBERED);
	try {
		localStorage.setItem(STORAGE_KEY, JSON.stringify(addresses));
	} catch {
		// Private window: the address is only kept until the editor closes.
	}
	return addresses;
}

/** The Wi-Fi entries of the port list, after the USB ports. */
export function wifiPorts(boardId: string, addresses: string[], labels: { accessPoint: string; home: string }): Port[] {
	if (!hasWifi(boardId)) return [];
	return [
		{ address: wifiPort(ACCESS_POINT_ADDRESS), label: labels.accessPoint },
		...addresses
			.filter((address) => address !== ACCESS_POINT_ADDRESS)
			.map((address) => ({ address: wifiPort(address), label: labels.home.replace("{address}", address) })),
	];
}
