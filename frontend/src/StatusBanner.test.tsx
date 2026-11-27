import { act } from "react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { translate, type MessageKey } from "@/i18n";
import { StatusBanner, type Status } from "@/StatusBanner";
import { button, renderUi, type Rendered } from "@/test/render";

const t = (key: MessageKey) => translate("fr", key);
const space = { left: 330, right: 110, bottom: "16px" };

let rendered: Rendered | null = null;
afterEach(() => {
	rendered?.unmount();
	rendered = null;
});

function show(status: Status, busy = false) {
	const onClose = vi.fn();
	rendered = renderUi(<StatusBanner status={status} busy={busy} space={space} t={t} onClose={onClose} />);
	return { onClose, box: document.querySelector<HTMLElement>(".s2a-status")! };
}

// Audit #211: the state was told by the colour only (WCAG 1.4.1).
describe("StatusBanner", () => {
	it("says in words and with an icon that something went wrong, and interrupts the screen reader", () => {
		const { box } = show({ kind: "error", message: "Le bloc \"répéter\" est vide." });
		const alert = box.querySelector('[role="alert"]')!;
		expect(alert).not.toBeNull();
		expect(alert.textContent).toContain(t("status.kind_error"));
		expect(alert.textContent).toContain("Le bloc \"répéter\" est vide.");
		expect(alert.querySelector(".mantine-Alert-icon svg")).not.toBeNull(); // the state icon, not the close X
	});

	it("tells a success and an information apart without colour, politely", () => {
		show({ kind: "success", message: "Programme envoyé." });
		expect(document.querySelector('[role="status"]')?.textContent).toContain(t("status.kind_success"));
		expect(document.querySelector('[role="alert"]')).toBeNull();
		rendered?.unmount();

		show({ kind: "info", message: "Projet ouvert." });
		expect(document.querySelector('[role="status"]')?.textContent).toContain(t("status.kind_info"));
		expect(t("status.kind_info")).not.toBe(t("status.kind_success"));
	});

	it("gives each new message a fresh alert, even with the same words, so it is announced again", () => {
		const onClose = vi.fn();
		const banner = (status: Status) => <StatusBanner status={status} busy={false} space={space} t={t} onClose={onClose} />;
		rendered = renderUi(banner({ kind: "info", message: "Vérification du programme..." }));
		const first = document.querySelector('[role="status"]');
		rendered.update(banner({ kind: "error", message: "Le bloc est vide." }));
		const error = document.querySelector('[role="alert"]');
		expect(error?.textContent).toContain("Le bloc est vide.");
		expect(first?.isConnected).toBe(false);
		rendered.update(banner({ kind: "error", message: "Le bloc est vide." }));
		expect(error?.isConnected).toBe(false);
		expect(document.querySelectorAll('[role="alert"]')).toHaveLength(1);
	});

	it("puts the technical details in a folded part, not in the message", () => {
		// jsdom lays nothing out, so the spoiler's "show" button (shown when the content is taller
		// than the fold) cannot appear here: the test checks where the details go.
		const { box } = show({ kind: "error", message: "Le téléversement a échoué.", details: "avrdude: stk500_recv(): programmer is not responding" });
		expect(box.querySelector(".mantine-Spoiler-root")?.textContent).toContain("stk500_recv");
		expect(box.querySelector(".mantine-Text-root")?.textContent).toBe("Le téléversement a échoué.");
	});

	it("shows a spinner while the action runs", () => {
		const { box } = show({ kind: "info", message: "Vérification du programme..." }, true);
		expect(box.querySelector(".mantine-Loader-root")).not.toBeNull();
	});

	it("keeps its place beside the panels in CSS variables, so a narrow window can drop them", () => {
		const { box } = show({ kind: "info", message: "Projet ouvert." });
		expect(box.style.getPropertyValue("--s2a-status-left")).toBe("330px");
		expect(box.style.getPropertyValue("--s2a-status-right")).toBe("110px");
		expect(box.style.getPropertyValue("--s2a-status-bottom")).toBe("16px");
		expect(box.style.left).toBe(""); // no fixed offset left inline
	});

	it("closes with a labelled button", () => {
		const { onClose } = show({ kind: "error", message: "Oups." });
		act(() => button(t("status.close"))!.click());
		expect(onClose).toHaveBeenCalled();
	});
});
