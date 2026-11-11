import { MantineProvider } from "@mantine/core";
import { act } from "react";
import { createRoot } from "react-dom/client";
import { describe, expect, it, vi } from "vitest";

import { translate, type MessageKey } from "@/i18n";
import { Tutorials } from "@/Tutorials";

describe("tutorials panel", () => {
	it("shows a tutorial, follows its links inside the panel and opens its example", async () => {
		(globalThis as { IS_REACT_ACT_ENVIRONMENT?: boolean }).IS_REACT_ACT_ENVIRONMENT = true;
		window.matchMedia ??= (() => ({
			matches: false,
			addEventListener() {},
			removeEventListener() {},
			addListener() {},
			removeListener() {},
		})) as never;
		Element.prototype.scrollIntoView ??= () => undefined;
		const onNavigate = vi.fn();
		const onOpenExample = vi.fn();
		const container = document.createElement("div");
		document.body.append(container);
		const root = createRoot(container);
		const t = (key: MessageKey, params?: Record<string, string>) => translate("fr", key, params);
		await act(async () =>
			root.render(
				<MantineProvider>
					<Tutorials
						language="fr"
						t={t}
						page="tutorials/01-blink.md"
						height="600px"
						onNavigate={onNavigate}
						onOpenExample={onOpenExample}
						onClose={() => undefined}
					/>
				</MantineProvider>
			)
		);
		expect(container.querySelector("h1")?.textContent).toBe("Tutoriel 1 — Clignotant");

		const next = [...container.querySelectorAll("a")].find((link) => link.textContent?.includes("Feu tricolore"));
		expect(next).toBeDefined();
		await act(async () => next?.click());
		expect(onNavigate).toHaveBeenCalledWith("tutorials/02-traffic-light.md", "fr");

		const english = [...container.querySelectorAll("a")].find((link) => link.getAttribute("href")?.includes("/en/"));
		await act(async () => english?.click());
		expect(onNavigate).toHaveBeenLastCalledWith("tutorials/01-blink.md", "en");

		const open = [...container.querySelectorAll("button")].find((button) => button.textContent === "Ouvrir l'exemple");
		await act(async () => open?.click());
		expect(onOpenExample).toHaveBeenCalledWith("blink");
		await act(async () => root.unmount());
	});
});
