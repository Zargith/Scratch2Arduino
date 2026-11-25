import type { Port } from "@/bridge";
import { findBoard } from "@/catalog";
import { isSimPort, SIM_PORT } from "@/simulation";
import { isWifiPort } from "@/wifi";

/**
 * Port to use after a new list of plugged-in boards. A Wi-Fi or Bluetooth port, or the virtual
 * board, stays chosen (they are not in the list); without any board, the virtual one is chosen.
 * `lost` names a real port that disappeared, so that the editor can say it (audit #191).
 */
export function choosePort(current: string | null, found: readonly Port[]): { port: string; lost: string | null } {
	const kept =
		current !== null &&
		(isWifiPort(current) ||
			current.startsWith("bt:") ||
			(isSimPort(current) && found.length === 0) ||
			found.some((candidate) => candidate.address === current));
	if (kept) return { port: current, lost: null };
	const lost = current !== null && !isSimPort(current) ? current : null;
	return { port: found[0]?.address ?? SIM_PORT, lost };
}

/** Board after a detection: the one of the first plugged-in board, when known. `replaced` names the board it replaces. */
export function detectedBoard(current: string, found: readonly Port[]): { boardId: string; replaced: string | null } {
	const detected = found[0]?.boardId;
	if (!detected || !findBoard(detected) || detected === current) return { boardId: current, replaced: null };
	return { boardId: detected, replaced: current };
}
