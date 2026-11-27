import { afterEach } from "vitest";

// What jsdom lacks and the editor needs, for every test (vite.config.ts setupFiles).
(globalThis as { IS_REACT_ACT_ENVIRONMENT?: boolean }).IS_REACT_ACT_ENVIRONMENT = true;

window.matchMedia ??= (() => ({
	matches: false,
	addEventListener() {},
	removeEventListener() {},
	addListener() {},
	removeListener() {},
})) as never;

globalThis.ResizeObserver ??= class {
	observe() {}
	unobserve() {}
	disconnect() {}
} as never;

// Panels scroll to their last line (serial monitor, Live output).
Element.prototype.scrollTo ??= function scrollTo() {} as never;

// CodeMirror measures text ranges, which jsdom does not lay out.
Range.prototype.getClientRects ??= () => [] as unknown as DOMRectList;
Range.prototype.getBoundingClientRect ??= () => new DOMRect();

// jsdom works out the whole CSS cascade (Mantine + Blockly sheets) on every getComputedStyle call,
// and Blockly calls it for each block it measures: mounting the editor took a minute or more
// (audit #201). jsdom lays nothing out anyway, so the inline style is all a test can rely on.
// A test can therefore not check a value that comes from a stylesheet (display, visibility...).
const emptyStyle = document.createElement("div").style;
window.getComputedStyle = ((element: Element) => (element as HTMLElement).style ?? emptyStyle) as never;

// Mantine keeps the colour scheme in localStorage, which jsdom leaves undefined without a URL.
if (typeof window.localStorage === "undefined" || window.localStorage === null) {
	const items = new Map<string, string>();
	Object.defineProperty(window, "localStorage", {
		configurable: true,
		value: {
			getItem: (key: string) => items.get(key) ?? null,
			setItem: (key: string, value: string) => void items.set(key, String(value)),
			removeItem: (key: string) => void items.delete(key),
			clear: () => items.clear(),
			key: (index: number) => [...items.keys()][index] ?? null,
			get length() {
				return items.size;
			},
		},
	});
}

afterEach(() => {
	window.localStorage.clear();
});
