import { MantineProvider } from "@mantine/core";
import { act, useState } from "react";
import { createRoot } from "react-dom/client";
import { describe, expect, it } from "vitest";

import { useSavedColorScheme } from "@/theme/colorScheme";

let rerender: () => void = () => undefined;

function Probe({ theme }: { theme: "" | "light" | "dark" }) {
	const [, setCount] = useState(0);
	rerender = () => setCount((count) => count + 1);
	useSavedColorScheme(theme);
	return null;
}

describe("saved appearance", () => {
	it("is applied when it changes, not at every render", async () => {
		(globalThis as { IS_REACT_ACT_ENVIRONMENT?: boolean }).IS_REACT_ACT_ENVIRONMENT = true;
		window.matchMedia ??= (() => ({
			matches: false,
			addEventListener() {},
			removeEventListener() {},
			addListener() {},
			removeListener() {},
		})) as never;
		// Mantine adds this style each time the scheme is set (transitions off for a moment).
		let applied = 0;
		const observer = new MutationObserver((records) => {
			for (const record of records) {
				for (const node of record.addedNodes) {
					if (node instanceof HTMLElement && node.hasAttribute("data-mantine-disable-transition")) applied += 1;
				}
			}
		});
		observer.observe(document.head, { childList: true });
		const container = document.createElement("div");
		document.body.append(container);
		const root = createRoot(container);
		const render = (theme: "" | "light" | "dark") =>
			act(async () =>
				root.render(
					<MantineProvider defaultColorScheme="auto">
						<Probe theme={theme} />
					</MantineProvider>
				)
			);
		const flush = () => act(async () => new Promise((resolve) => setTimeout(resolve, 0)));

		await render("dark");
		await flush();
		expect(document.documentElement.getAttribute("data-mantine-color-scheme")).toBe("dark");
		const once = applied;
		expect(once).toBeGreaterThan(0);

		for (let i = 0; i < 5; i += 1) await act(async () => rerender());
		await flush();
		expect(applied).toBe(once); // nothing set again while the choice stays the same

		await render("light");
		await flush();
		expect(document.documentElement.getAttribute("data-mantine-color-scheme")).toBe("light");
		expect(applied).toBe(once + 1);

		observer.disconnect();
		await act(async () => root.unmount());
	});
});
