import { MantineProvider } from "@mantine/core";
import { act } from "react";
import { createRoot } from "react-dom/client";
import { describe, expect, it, vi } from "vitest";

import type { SimState, WiringPlan } from "@/bridge";
import { translate, type MessageKey } from "@/i18n";
import { Simulator } from "@/Simulator";

const plan: WiringPlan = {
	components: [
		{
			kind: "button",
			name: "Bouton poussoir",
			connections: [{ componentPin: "1", boardPin: "2", via: "", role: "PIN" }],
			note: "",
			blockIds: [],
		},
		{
			kind: "lcd16",
			name: "Écran",
			connections: [{ componentPin: "RS", boardPin: "12", via: "", role: "RS_PIN" }],
			note: "",
			blockIds: [],
		},
	],
	warnings: [],
};

const state: SimState = {
	modes: { "13": "output" },
	outputs: { "13": { level: 255, pwm: false } },
	servos: {},
	tones: {},
	lcd: ["", ""],
	lcd16: ["Score 3", ""],
	oled: [],
	pixels: {},
	matrices: {},
	phone: ["salut"],
	pressed: {},
	analog: {},
	distances: {},
	climates: {},
};

describe("virtual board panel", () => {
	it("shows the outputs and sends the inputs", async () => {
		(globalThis as { IS_REACT_ACT_ENVIRONMENT?: boolean }).IS_REACT_ACT_ENVIRONMENT = true;
		window.matchMedia ??= (() => ({
			matches: false,
			addEventListener() {},
			removeEventListener() {},
			addListener() {},
			removeListener() {},
		})) as never;
		const onInput = vi.fn();
		const t = (key: MessageKey, params?: Record<string, string>) => translate("fr", key, params);
		const container = document.createElement("div");
		document.body.append(container);
		const root = createRoot(container);
		await act(async () =>
			root.render(
				<MantineProvider>
					<Simulator plan={plan} state={state} phone t={t} onInput={onInput} />
				</MantineProvider>
			)
		);
		const text = container.textContent ?? "";
		expect(text).toContain("Carte virtuelle");
		expect(text).toContain("Score 3");
		expect(text).toContain("Broches de la carte");
		expect(text).toContain("salut");
		expect(container.querySelector('[aria-label^="Broche 13 : sortie"]')).not.toBeNull();

		const press = [...container.querySelectorAll("button")].find((button) => button.textContent === "Appuie");
		expect(press).toBeDefined();
		await act(async () => press?.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true })));
		expect(onInput).toHaveBeenLastCalledWith("press", "2", true);
		await act(async () => press?.dispatchEvent(new PointerEvent("pointerup", { bubbles: true })));
		expect(onInput).toHaveBeenLastCalledWith("press", "2", false);

		const input = container.querySelector<HTMLInputElement>('input[aria-label="Message envoyé à la carte"]');
		expect(input).not.toBeNull();
		await act(async () => {
			const setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value")?.set;
			setter?.call(input, "Allume");
			input?.dispatchEvent(new Event("input", { bubbles: true }));
		});
		await act(async () => input?.form?.requestSubmit());
		expect(onInput).toHaveBeenLastCalledWith("phone", "Allume");
		await act(async () => root.unmount());
	});
});
