import { describe, expect, it } from "vitest";

import { translate, type MessageKey } from "@/i18n";
import { notAppliedHint } from "@/liveUpdate";

const t = (key: MessageKey, params?: Record<string, string>) => translate("en", key, params);

// Audit 2026-11-24 (#179): half-built blocks in Live mode were ignored without a word.
describe("Live update result", () => {
	it("explains why the board keeps the previous blocks", () => {
		expect(notAppliedHint({ ok: false, message: "The wait block has an empty slot." }, t)).toBe(
			"Your changes are not sent to the board yet: The wait block has an empty slot."
		);
	});

	it("says nothing when the blocks were sent, or with an older host", () => {
		expect(notAppliedHint({ ok: true }, t)).toBeNull();
		expect(notAppliedHint(undefined, t)).toBeNull();
		expect(notAppliedHint({ ok: false }, t)).toBe("Your changes are not sent to the board yet: ");
	});
});
