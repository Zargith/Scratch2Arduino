import { MantineProvider } from "@mantine/core";
import { act } from "react";
import { createRoot, type Root } from "react-dom/client";
import { afterEach, describe, expect, it } from "vitest";

import { translate, type MessageKey } from "@/i18n";
import { ZoomableImage } from "@/ZoomableImage";

let container: HTMLDivElement | null = null;
let root: Root | null = null;
afterEach(() => {
	act(() => root?.unmount());
	container?.remove();
	container = null;
	root = null;
});

function show() {
	container = document.createElement("div");
	document.body.appendChild(container);
	const t = (key: MessageKey) => translate("en", key);
	act(() => {
		root = createRoot(container!);
		root.render(
			<MantineProvider>
				<ZoomableImage src="data:image/svg+xml,<svg/>" alt="Wiring" height="200px" t={t} />
			</MantineProvider>
		);
	});
	const frame = container.querySelector<HTMLElement>("[role='group']")!;
	/** Presses `key` on the frame; true when the frame took it (default prevented). */
	const press = (key: string, modifiers: KeyboardEventInit = {}) => {
		const event = new KeyboardEvent("keydown", { key, bubbles: true, cancelable: true, ...modifiers });
		act(() => {
			frame.dispatchEvent(event);
		});
		return event.defaultPrevented;
	};
	return { frame, press, text: () => container!.textContent ?? "" };
}

// Audit 2026-11-24 (#211): the wiring diagram could only be zoomed and moved with a pointer.
describe("ZoomableImage keyboard", () => {
	it("zooms with + and - and shows it all again with 0", () => {
		const { frame, press, text } = show();
		expect(frame.tabIndex).toBe(0);
		expect(frame.getAttribute("aria-label")).toContain("Wiring");
		expect(press("+")).toBe(true);
		expect(text()).toContain("150 %");
		press("-");
		expect(text()).toContain("100 %");
		press("+");
		press("0");
		expect(text()).toContain("100 %");
	});

	it("leaves Ctrl/Cmd zoom and Alt shortcuts to the system", () => {
		const { press, text } = show();
		expect(press("+", { ctrlKey: true })).toBe(false);
		expect(press("-", { metaKey: true })).toBe(false);
		expect(press("ArrowLeft", { altKey: true })).toBe(false);
		expect(text()).toContain("100 %");
	});

	it("takes the arrows only once zoomed in", () => {
		const { press } = show();
		expect(press("ArrowDown")).toBe(false); // nothing to move: the page scrolls
		press("+");
		expect(press("ArrowDown")).toBe(true);
	});
});
