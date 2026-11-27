import { act } from "react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { examples } from "@/examples";
import { Gallery } from "@/Gallery";
import { translate, type MessageKey } from "@/i18n";
import { renderUi, type Rendered } from "@/test/render";

let rendered: Rendered | null = null;
afterEach(() => {
	rendered?.unmount();
	rendered = null;
});

function show() {
	const handlers = { onBlank: vi.fn(), onExample: vi.fn(), onTutorial: vi.fn(), onClose: vi.fn() };
	const t = (key: MessageKey, params?: Record<string, string>) => translate("fr", key, params);
	rendered = renderUi(<Gallery opened language="fr" t={t} {...handlers} />);
	return handlers;
}

const buttons = (text: string) =>
	[...document.querySelectorAll<HTMLButtonElement>("button")].filter((candidate) => candidate.textContent?.trim() === text);

// Audit #201: the example gallery had no test.
describe("Gallery", () => {
	it("shows a card for the blank project and for each example group", () => {
		show();
		const groups = new Set(examples.map((example) => example.group));
		expect(buttons("Commencer")).toHaveLength(1 + groups.size);
		expect(document.body.textContent).toContain(examples[0]!.title.fr);
	});

	it("starts a blank project, an example or its tutorial", () => {
		const { onBlank, onExample, onTutorial } = show();
		const [blank, first] = buttons("Commencer");
		act(() => blank!.click());
		expect(onBlank).toHaveBeenCalled();
		act(() => first!.click());
		expect(onExample).toHaveBeenCalledWith(examples[0]);
		const tutorial = buttons("Tutoriel pas à pas")[0];
		expect(tutorial).toBeDefined();
		act(() => tutorial!.click());
		expect(onTutorial).toHaveBeenCalledTimes(1);
	});
});
