import { describe, expect, it } from "vitest";

import { choosePort, detectedBoard } from "@/ports";
import { SIM_PORT } from "@/simulation";

const uno = { address: "/dev/ttyACM0", label: "Arduino Uno", boardId: "uno" };
const nano = { address: "/dev/ttyUSB0", label: "USB serial", boardId: "nano" };

// Audit 2026-11-24 (#191): a lost port and a detected board silently replaced the user's choice.
describe("port and board after a detection", () => {
	it("keeps a port still plugged in, and Wi-Fi, Bluetooth or the virtual board", () => {
		expect(choosePort(uno.address, [uno, nano])).toEqual({ port: uno.address, lost: null });
		expect(choosePort("wifi:192.168.4.1", [])).toEqual({ port: "wifi:192.168.4.1", lost: null });
		expect(choosePort("bt:S2A-1", [uno])).toEqual({ port: "bt:S2A-1", lost: null });
		expect(choosePort(SIM_PORT, [])).toEqual({ port: SIM_PORT, lost: null });
	});

	it("says which real port was lost", () => {
		expect(choosePort(uno.address, [])).toEqual({ port: SIM_PORT, lost: uno.address });
		expect(choosePort(uno.address, [nano])).toEqual({ port: nano.address, lost: uno.address });
	});

	it("takes the first board at start, or when the virtual board is chosen and a board is plugged in", () => {
		expect(choosePort(null, [nano, uno])).toEqual({ port: nano.address, lost: null });
		expect(choosePort(SIM_PORT, [uno])).toEqual({ port: uno.address, lost: null });
	});

	it("tells when the detected board replaces the chosen one", () => {
		expect(detectedBoard("mega", [nano])).toEqual({ boardId: "nano", replaced: "mega" });
		expect(detectedBoard("nano", [nano])).toEqual({ boardId: "nano", replaced: null });
		expect(detectedBoard("mega", [{ address: "/dev/ttyS0", label: "?" }])).toEqual({ boardId: "mega", replaced: null });
		expect(detectedBoard("mega", [{ ...nano, boardId: "not-a-board" }])).toEqual({ boardId: "mega", replaced: null });
	});
});
