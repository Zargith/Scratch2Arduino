// Bluetooth "ports" of the boards that have Bluetooth (ESP32): the Live mode reaches them without
// the USB cable. The C++ host recognises the "bt:" prefix (core/include/s2a/network.hpp). On
// macOS the board is paired in the system settings and appears as a serial port instead.
import type { Port } from "@/bridge";
import { findBoard } from "@/catalog";

export const BLUETOOTH_PREFIX = "bt:";
const STORAGE_KEY = "s2a.bluetooth.boards";
const REMEMBERED = 3;

export interface BluetoothBoard {
	name: string;
	address: string;
}

/** Boards whose Live firmware has Bluetooth ("bluetooth" in resources/boards/<id>.json). */
export const hasBluetooth = (boardId: string): boolean => findBoard(boardId)?.bluetooth ?? false;
export const bluetoothPort = (address: string): string => `${BLUETOOTH_PREFIX}${address}`;
/** A "bt:" port, or the serial port of a board paired on macOS (/dev/cu.S2A-1A2B). */
export const isBluetoothPort = (port: string | null): boolean =>
	(port?.startsWith(BLUETOOTH_PREFIX) ?? false) || (port?.includes("S2A-") ?? false);

const isBoard = (item: unknown): item is BluetoothBoard =>
	typeof item === "object" &&
	item !== null &&
	typeof (item as BluetoothBoard).name === "string" &&
	typeof (item as BluetoothBoard).address === "string";

/** Boards found by a Bluetooth search, most recent first (kept in this browser only). */
export function rememberedBoards(): BluetoothBoard[] {
	try {
		const stored: unknown = JSON.parse(localStorage.getItem(STORAGE_KEY) ?? "[]");
		return Array.isArray(stored) ? stored.filter(isBoard) : [];
	} catch {
		return [];
	}
}

export function rememberBoard(board: BluetoothBoard): BluetoothBoard[] {
	const boards = [board, ...rememberedBoards().filter((known) => known.address !== board.address)].slice(0, REMEMBERED);
	try {
		localStorage.setItem(STORAGE_KEY, JSON.stringify(boards));
	} catch {
		// Private window: the board is only kept until the editor closes.
	}
	return boards;
}

/** The Bluetooth entries of the port list, after the USB and Wi-Fi ones. */
export function bluetoothPorts(boardId: string, boards: BluetoothBoard[], label: string): Port[] {
	if (!hasBluetooth(boardId)) return [];
	return boards.map((board) => ({ address: bluetoothPort(board.address), label: label.replace("{name}", board.name) }));
}
