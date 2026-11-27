import { act } from "react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { translate, type MessageKey } from "@/i18n";
import { clearHost, installHost, waitUntil } from "@/test/app";
import { button, input, renderUi, typeInto, type Rendered } from "@/test/render";
import { WifiDialog } from "@/WifiDialog";

let rendered: Rendered | null = null;
afterEach(() => {
	rendered?.unmount();
	rendered = null;
	clearHost();
});

const t = (key: MessageKey, params?: Record<string, string>) => translate("fr", key, params);

function show(usbPort: string | null, answer: () => Promise<unknown>) {
	const sent: unknown[][] = [];
	installHost({
		s2a_hello: async () => ({ language: "fr", version: "test" }),
		s2a_wifi_setup: async (...args: never[]) => {
			sent.push(args);
			return answer();
		},
	});
	const onJoined = vi.fn();
	rendered = renderUi(
		<WifiDialog opened language="fr" boardId="esp32" usbPort={usbPort} t={t} onJoined={onJoined} onClose={vi.fn()} />
	);
	return { sent, onJoined };
}

// Audit #201: the Wi-Fi window had no test.
describe("WifiDialog", () => {
	it("sends the home network over USB and gives the board's address", async () => {
		const { sent, onJoined } = show("COM5", async () => ({ ok: true, message: "La carte a rejoint Maison.", address: "192.168.1.42" }));
		expect(button("Envoyer à la carte")?.disabled).toBe(true); // no name yet
		typeInto(input("Nom du réseau Wi-Fi")!, "  Maison ");
		typeInto(input("Mot de passe")!, "secret");
		act(() => button("Envoyer à la carte")!.click());
		await waitUntil(() => document.body.textContent!.includes("La carte a rejoint Maison."), "the answer");
		expect(sent).toEqual([["COM5", "Maison", "secret", "esp32", "fr"]]);
		expect(onJoined).toHaveBeenCalledWith("192.168.1.42");
	});

	it("shows why the board did not join, without an address", async () => {
		const { onJoined } = show("COM5", async () => ({ ok: false, message: "Mot de passe refusé." }));
		typeInto(input("Nom du réseau Wi-Fi")!, "Maison");
		act(() => button("Envoyer à la carte")!.click());
		await waitUntil(() => document.body.textContent!.includes("Mot de passe refusé."), "the answer");
		expect(onJoined).not.toHaveBeenCalled();
	});

	it("needs the board on USB to send anything", () => {
		show(null, async () => ({ ok: true, message: "" }));
		typeInto(input("Nom du réseau Wi-Fi")!, "Maison");
		expect(button("Envoyer à la carte")?.disabled).toBe(true);
		expect(document.body.textContent).toContain(t("wifi.no_usb"));
	});
});
