import { Badge, MantineProvider } from "@mantine/core";
import { act } from "react";
import { createRoot, type Root } from "react-dom/client";
import { afterEach, describe, expect, it } from "vitest";

import { theme } from "@/theme/theme";

let root: Root | null = null;
afterEach(() => {
	act(() => root?.unmount());
	root = null;
	document.body.innerHTML = "";
});

// Audit #211: about 70 texts were 12px and badges 9 to 11px capitals, for children of 8 to 14.
describe("theme text sizes", () => {
	it("makes the smallest text size 14px", () => {
		expect(theme.fontSizes?.xs).toBe("0.875rem");
	});

	it("writes badges at 14px in normal case", () => {
		const container = document.body.appendChild(document.createElement("div"));
		act(() => {
			root = createRoot(container);
			root.render(
				<MantineProvider theme={theme}>
					<Badge>installées</Badge>
				</MantineProvider>
			);
		});
		const badge = container.querySelector<HTMLElement>(".mantine-Badge-root")!;
		expect(badge.style.getPropertyValue("--badge-fz")).toBe("var(--mantine-font-size-sm)");
		expect(badge.style.textTransform).toBe("none");
		expect(badge.dataset.size).toBe("lg");
	});
});
