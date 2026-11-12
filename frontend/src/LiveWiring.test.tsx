import { MantineProvider } from "@mantine/core";
import { act } from "react";
import { createRoot } from "react-dom/client";
import { describe, expect, it, vi } from "vitest";

import { translate, type MessageKey } from "@/i18n";
import { LiveWiring } from "@/LiveWiring";

describe("wiring check of the Live mode", () => {
	it("lists the hints and shows the blocks of one", async () => {
		(globalThis as { IS_REACT_ACT_ENVIRONMENT?: boolean }).IS_REACT_ACT_ENVIRONMENT = true;
		window.matchMedia ??= (() => ({
			matches: false,
			addEventListener() {},
			removeEventListener() {},
			addListener() {},
			removeListener() {},
		})) as never;
		const onShowBlocks = vi.fn();
		const onClose = vi.fn();
		const t = (key: MessageKey, params?: Record<string, string>) => translate("fr", key, params);
		const container = document.createElement("div");
		document.body.append(container);
		const root = createRoot(container);
		await act(async () =>
			root.render(
				<MantineProvider>
					<LiveWiring
						hints={[{ message: "Le bouton de la broche 2 semble toujours appuyé.", blockIds: ["button"] }]}
						t={t}
						onShowBlocks={onShowBlocks}
						onClose={onClose}
					/>
				</MantineProvider>
			)
		);
		expect(container.textContent).toContain("Le bouton de la broche 2");
		const show = [...container.querySelectorAll("button")].find((button) => button.textContent === t("live.wiring_show_blocks"));
		await act(async () => show?.click());
		expect(onShowBlocks).toHaveBeenCalledWith(["button"]);
		await act(async () => container.querySelector<HTMLButtonElement>(`button[aria-label="${t("live.wiring_close")}"]`)?.click());
		expect(onClose).toHaveBeenCalled();
		await act(async () => root.unmount());
	});
});
