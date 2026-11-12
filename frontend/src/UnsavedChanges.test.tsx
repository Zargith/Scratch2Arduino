import { MantineProvider } from "@mantine/core";
import { act } from "react";
import { createRoot } from "react-dom/client";
import { describe, expect, it, vi } from "vitest";

import { translate, type MessageKey } from "@/i18n";
import { UnsavedChanges } from "@/UnsavedChanges";

describe("unsaved changes question", () => {
	it("offers to save, ignore or cancel", async () => {
		(globalThis as { IS_REACT_ACT_ENVIRONMENT?: boolean }).IS_REACT_ACT_ENVIRONMENT = true;
		window.matchMedia ??= (() => ({
			matches: false,
			addEventListener() {},
			removeEventListener() {},
			addListener() {},
			removeListener() {},
		})) as never;
		const onChoice = vi.fn();
		const t = (key: MessageKey, params?: Record<string, string>) => translate("fr", key, params);
		const container = document.createElement("div");
		document.body.append(container);
		const root = createRoot(container);
		await act(async () =>
			root.render(
				<MantineProvider>
					<UnsavedChanges opened t={t} onChoice={onChoice} />
				</MantineProvider>
			)
		);
		expect(document.body.textContent).toContain("Enregistrer le projet ?");
		const button = (label: string) => [...document.querySelectorAll("button")].find((item) => item.textContent === label);
		for (const [label, choice] of [
			["Enregistrer", "save"],
			["Ignorer", "discard"],
			["Annuler", "cancel"],
		] as const) {
			await act(async () => button(label)?.click());
			expect(onChoice).toHaveBeenLastCalledWith(choice);
		}
		await act(async () => root.unmount());
	});
});
