import { describe, expect, it } from "vitest";

import { reportUrl, systemName } from "@/report";

describe("report a problem", () => {
	it("names the operating system", () => {
		expect(systemName("Mozilla/5.0 (Windows NT 10.0; Win64; x64)")).toBe("Windows");
		expect(systemName("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)")).toBe("macOS");
		expect(systemName("Mozilla/5.0 (X11; Linux x86_64)")).toBe("Linux");
		expect(systemName("")).toBe("");
	});

	it("fills the issue form fields it knows", () => {
		const url = new URL(reportUrl({ version: "0.14.0", board: "Arduino Uno" }, "Mozilla/5.0 (X11; Linux x86_64)"));
		expect(url.origin + url.pathname).toBe("https://github.com/Zargith/Scratch2Arduino/issues/new");
		expect(url.searchParams.get("template")).toBe("bug_report.yml");
		expect(url.searchParams.get("version")).toBe("0.14.0");
		expect(url.searchParams.get("os")).toBe("Linux");
		expect(url.searchParams.get("board")).toBe("Arduino Uno");
		expect(url.searchParams.has("what-happened")).toBe(false);
	});

	it("leaves out empty fields and shortens long error details", () => {
		const url = new URL(reportUrl({ version: "", board: "", details: "x".repeat(4000) }, ""));
		expect(url.searchParams.has("version")).toBe(false);
		expect(url.searchParams.has("board")).toBe(false);
		const details = url.searchParams.get("what-happened") ?? "";
		expect(details).toContain("[...]");
		expect(details.length).toBeLessThan(1600);
	});
});
