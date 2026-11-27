// Renders one component (with Mantine) for its tests, and types into its inputs like a user.
import { MantineProvider } from "@mantine/core";
import { act, type ReactElement } from "react";
import { createRoot, type Root } from "react-dom/client";

export interface Rendered {
	/** Re-renders with new props. */
	update(ui: ReactElement): void;
	unmount(): void;
}

export function renderUi(ui: ReactElement): Rendered {
	const container = document.createElement("div");
	document.body.appendChild(container);
	let root: Root | null = null;
	act(() => {
		root = createRoot(container);
		root.render(<MantineProvider>{ui}</MantineProvider>);
	});
	return {
		update(next) {
			act(() => root?.render(<MantineProvider>{next}</MantineProvider>));
		},
		unmount() {
			act(() => root?.unmount());
			container.remove();
		},
	};
}

/** Types `value` into a React-controlled input (sets it through the native setter, then "input"). */
export function typeInto(input: HTMLInputElement, value: string): void {
	const setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value")?.set;
	act(() => {
		setter?.call(input, value);
		input.dispatchEvent(new Event("input", { bubbles: true }));
	});
}

/** The button (anywhere in the page: modals are portals) whose text or label is `name`. */
export function button(name: string): HTMLButtonElement | undefined {
	return [...document.querySelectorAll<HTMLButtonElement>("button")].find(
		(candidate) => candidate.textContent?.trim() === name || candidate.getAttribute("aria-label") === name
	);
}

/** The input whose label (or aria-label) is `name`. */
export function input(name: string): HTMLInputElement | undefined {
	const byAria = [...document.querySelectorAll<HTMLInputElement>("input[aria-label]")].find(
		(candidate) => candidate.getAttribute("aria-label") === name
	);
	if (byAria) return byAria;
	const label = [...document.querySelectorAll("label")].find((candidate) => candidate.textContent?.trim() === name);
	const id = label?.getAttribute("for");
	return id ? (document.getElementById(id) as HTMLInputElement | null) ?? undefined : undefined;
}
