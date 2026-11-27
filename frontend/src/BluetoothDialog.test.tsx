import { act } from "react";
import { afterEach, describe, expect, it, vi } from "vitest";

import type { HostFunctions } from "@/bridge";
import { BluetoothDialog } from "@/BluetoothDialog";
import { translate, type MessageKey } from "@/i18n";
import { clearHost, installHost, waitUntil } from "@/test/app";
import { button, renderUi, type Rendered } from "@/test/render";

let rendered: Rendered | null = null;
afterEach(() => {
	rendered?.unmount();
	rendered = null;
	clearHost();
});

const t = (key: MessageKey, params?: Record<string, string>) => translate("fr", key, params);

function show(boardId: string, usbPort: string | null, host: Record<string, (...args: never[]) => Promise<unknown>>) {
	installHost({ s2a_hello: async () => ({ language: "fr", version: "test" }), ...host });
	const onChosen = vi.fn();
	const onClose = vi.fn();
	rendered = renderUi(
		<BluetoothDialog opened language="fr" boardId={boardId} usbPort={usbPort} t={t} onChosen={onChosen} onClose={onClose} />
	);
	return { onChosen, onClose };
}

const robot = { name: "Robot", address: "24:6F:28:AA:BB:CC", port: "bt:24:6F:28:AA:BB:CC" };

// Audit #201: the Bluetooth window had no test.
describe("BluetoothDialog", () => {
	it("finds the boards around and gives the chosen one", async () => {
		const { onChosen, onClose } = show("esp32", null, { s2a_bluetooth_search: async () => ({ ok: true, devices: [robot] }) });
		act(() => button("Chercher les cartes")!.click());
		await waitUntil(() => document.body.textContent!.includes("24:6F:28:AA:BB:CC"), "the board found");
		act(() => button("Choisir")!.click());
		expect(onChosen).toHaveBeenCalledWith(robot);
		expect(onClose).toHaveBeenCalled();
	});

	it("says when no board answers", async () => {
		show("esp32", null, { s2a_bluetooth_search: async () => ({ ok: true, devices: [] }) });
		act(() => button("Chercher les cartes")!.click());
		await waitUntil(() => document.body.textContent!.includes(t("bluetooth.none")), "the empty result");
	});

	it("says why a search failed", async () => {
		show("esp32", null, { s2a_bluetooth_search: async () => ({ ok: false, message: "Le Bluetooth est éteint." }) });
		act(() => button("Chercher les cartes")!.click());
		await waitUntil(() => document.body.textContent!.includes("Le Bluetooth est éteint."), "the failure");
	});

	it("saves the HC-05 pins on an Arduino board over USB", async () => {
		const saved: Parameters<HostFunctions["s2a_bluetooth_module"]>[] = [];
		show("uno", "COM3", {
			s2a_bluetooth_module: async (...args: Parameters<HostFunctions["s2a_bluetooth_module"]>) => {
				saved.push(args);
				return { ok: true, message: "Module enregistré sur la carte." };
			},
		});
		act(() => button("Enregistrer sur la carte")!.click());
		await waitUntil(() => document.body.textContent!.includes("Module enregistré sur la carte."), "the answer");
		expect(saved).toEqual([["COM3", "uno", { rx: "10", tx: "11", baud: 9600 }, "fr"]]);
	});

	it("needs the Arduino board on USB to save the module", () => {
		show("uno", null, {});
		expect(button("Enregistrer sur la carte")?.disabled).toBe(true);
		expect(document.body.textContent).toContain(t("bluetooth.module_no_usb"));
	});
});
