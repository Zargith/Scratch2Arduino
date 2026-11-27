import { act } from "react";
import { afterEach, describe, expect, it, vi } from "vitest";

import type { HostFunctions } from "@/bridge";
import { translate, type MessageKey } from "@/i18n";
import { SerialMonitor } from "@/SerialMonitor";
import { clearHost, installHost, waitUntil } from "@/test/app";
import { button, input, renderUi, typeInto, type Rendered } from "@/test/render";

let rendered: Rendered | null = null;
afterEach(() => {
	rendered?.unmount();
	rendered = null;
	clearHost();
});

const t = (key: MessageKey, params?: Record<string, string>) => translate("fr", key, params);

/** A board on COM3 that has printed `lines`; what the editor sends is kept in `sent`. */
function board(lines: string[], options: { openMessage?: string; sendFails?: boolean } = {}) {
	const calls = { opened: [] as Parameters<HostFunctions["s2a_monitor_open"]>[], sent: [] as string[], closed: 0 };
	installHost({
		s2a_hello: async () => ({ language: "fr", version: "test" }),
		s2a_monitor_open: async (...args: Parameters<HostFunctions["s2a_monitor_open"]>) => {
			calls.opened.push(args);
			return options.openMessage ? { ok: false, message: options.openMessage } : { ok: true };
		},
		s2a_monitor_state: async () => ({ open: true, lines }),
		s2a_monitor_send: async (line: string) => {
			if (options.sendFails) throw new Error("Le port est fermé.");
			calls.sent.push(line);
		},
		s2a_monitor_close: async () => {
			calls.closed += 1;
		},
	});
	return calls;
}

function show(props: { busy?: boolean; onError?: (message: string) => void } = {}) {
	const element = (busy: boolean) => (
		<SerialMonitor
			t={t}
			language="fr"
			port="COM3"
			live={false}
			liveLines={[]}
			busy={busy}
			onError={props.onError ?? vi.fn()}
			onClose={vi.fn()}
		/>
	);
	rendered = renderUi(element(props.busy ?? false));
	return { setBusy: (busy: boolean) => rendered?.update(element(busy)) };
}

// Audit #201: the serial monitor had no test.
describe("SerialMonitor", () => {
	it("connects to the board, shows what it prints and sends a line", async () => {
		const calls = board(["Bonjour", "Température : 21"]);
		show();
		act(() => button("Connecter")!.click());
		await waitUntil(() => document.body.textContent!.includes("Température : 21"), "the board's lines");
		expect(calls.opened).toEqual([["COM3", 9600, "fr"]]);

		const field = input("Texte à envoyer à la carte")!;
		typeInto(field, "allume");
		act(() => button("Envoyer")!.click());
		await waitUntil(() => calls.sent.length > 0, "the line sent");
		expect(calls.sent).toEqual(["allume"]);
		expect(field.value).toBe("");
	});

	it("gives back a line that could not be sent and says why (#190)", async () => {
		board([], { sendFails: true });
		const onError = vi.fn();
		show({ onError });
		act(() => button("Connecter")!.click());
		await waitUntil(() => !input("Texte à envoyer à la carte")!.disabled, "the connection");
		typeInto(input("Texte à envoyer à la carte")!, "allume");
		act(() => button("Envoyer")!.click());
		await waitUntil(() => onError.mock.calls.length > 0, "the error");
		expect(onError).toHaveBeenCalledWith("Le port est fermé.");
		expect(input("Texte à envoyer à la carte")!.value).toBe("allume");
	});

	it("says why the port could not be opened and offers to connect again", async () => {
		board([], { openMessage: "Impossible d'utiliser le port de la carte." });
		const onError = vi.fn();
		show({ onError });
		act(() => button("Connecter")!.click());
		await waitUntil(() => onError.mock.calls.length > 0, "the error");
		expect(onError.mock.calls[0]![0]).toBe("Impossible d'utiliser le port de la carte.");
		await waitUntil(() => button("Connecter") !== undefined, "the Connect button");
	});

	it("waits for an upload to end before connecting", async () => {
		const calls = board(["prêt"]);
		const { setBusy } = show({ busy: true });
		act(() => button("Connecter")!.click());
		await act(async () => new Promise((done) => setTimeout(done, 100)));
		expect(calls.opened).toHaveLength(0);
		expect(document.body.textContent).toContain("En pause pendant l'envoi du programme...");
		setBusy(false);
		await waitUntil(() => document.body.textContent!.includes("prêt"), "the connection after the upload");
		expect(calls.opened).toHaveLength(1);
	});

	it("frees the port when it is closed", async () => {
		const calls = board(["connectée"]);
		show();
		act(() => button("Connecter")!.click());
		await waitUntil(() => document.body.textContent!.includes("connectée"), "the connection");
		expect(calls.closed).toBe(0);
		rendered?.unmount();
		rendered = null;
		await waitUntil(() => calls.closed > 0, "the port closed");
	});
});
