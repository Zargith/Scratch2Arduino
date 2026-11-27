import { describe, expect, it, vi } from "vitest";
import { promptSlot } from "./promptSlot";

describe("promptSlot", () => {
	it("answers the open question", () => {
		const slot = promptSlot();
		const resolve = vi.fn();
		slot.show({ resolve });
		slot.answer("blink");
		expect(resolve).toHaveBeenCalledWith("blink");
	});

	it("cancels an open question when another one is shown", () => {
		// Audit #196: a second Blockly prompt replaced the first without answering it.
		const slot = promptSlot();
		const first = vi.fn();
		const second = vi.fn();
		slot.show({ resolve: first });
		slot.show({ resolve: second });
		expect(first).toHaveBeenCalledWith(null);
		slot.answer("ok");
		expect(second).toHaveBeenCalledWith("ok");
		expect(first).toHaveBeenCalledTimes(1);
	});

	it("answers a question only once", () => {
		const slot = promptSlot();
		const resolve = vi.fn();
		slot.show({ resolve });
		slot.answer("a");
		slot.answer(null);
		expect(resolve).toHaveBeenCalledTimes(1);
	});

	it("does nothing when no question is open", () => {
		expect(() => promptSlot().answer(null)).not.toThrow();
	});

	it("keeps a question shown by the answer of the previous one", () => {
		// Creating a function asks for its name, then may ask another question at once.
		const slot = promptSlot();
		const followUp = vi.fn();
		slot.show({ resolve: () => slot.show({ resolve: followUp }) });
		slot.answer("blink");
		slot.answer("next");
		expect(followUp).toHaveBeenCalledWith("next");
	});
});
