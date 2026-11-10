import { MantineProvider } from "@mantine/core";
import { act } from "react";
import { createRoot } from "react-dom/client";
import { describe, expect, it, vi } from "vitest";

import type { WiringPlan } from "@/bridge";
import { translate, type MessageKey } from "@/i18n";
import { emptyProject } from "@/project/convert";
import { ProjectWiring } from "@/ProjectWiring";

const plan: WiringPlan = {
	components: [
		{
			kind: "button",
			name: "Push button",
			connections: [
				{ componentPin: "one leg", boardPin: "7", via: "" },
				{ componentPin: "opposite leg", boardPin: "GND", via: "" },
			],
			note: "No resistor: the board uses its internal pull-up resistor.",
			blockIds: ["press"],
		},
	],
	warnings: [{ message: "Pin 7 is used by both", blockIds: ["press", "led"] }],
};

describe("project wiring window", () => {
	it("shows the components, their pins and the warnings computed by the core", async () => {
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
		const host = window as unknown as Record<string, unknown>;
		host.s2a_hello = async () => ({ language: "en", version: "test" });
		const asked = vi.fn(async () => plan);
		host.s2a_wiring = asked;
		const shown = vi.fn();
		const t = (key: MessageKey, params?: Record<string, string>) => translate("en", key, params);

		const container = document.createElement("div");
		document.body.appendChild(container);
		const root = createRoot(container);
		await act(async () => {
			root.render(
				<MantineProvider>
					<ProjectWiring
						opened
						example={null}
						language="en"
						project={() => emptyProject("Test", "uno")}
						t={t}
						onShowBlocks={shown}
						onClose={() => undefined}
					/>
				</MantineProvider>
			);
		});
		await act(async () => {
			await new Promise((done) => setTimeout(done, 50));
		});
		expect(asked).toHaveBeenCalledTimes(1);
		const text = document.body.textContent ?? "";
		for (const expected of ["Push button", "one leg", "GND", "No resistor", "Pin 7 is used by both"]) {
			expect(text).toContain(expected);
		}
		const buttons = [...document.body.querySelectorAll("button")].filter((button) =>
			button.textContent?.includes("Show the blocks")
		);
		expect(buttons.length).toBe(2);
		await act(async () => buttons[0]?.click());
		expect(shown).toHaveBeenCalledWith(["press", "led"]);
		await act(async () => root.unmount());
		container.remove();
	}, 60_000);
});
