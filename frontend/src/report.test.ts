import { describe, expect, it } from "vitest";

import { reportBody, reportUrl, systemName } from "@/report";

describe("report a problem", () => {
	it("names the operating system", () => {
		expect(systemName("Mozilla/5.0 (Windows NT 10.0; Win64; x64)")).toBe("Windows");
		expect(systemName("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)")).toBe("macOS");
		expect(systemName("Mozilla/5.0 (X11; Linux x86_64)")).toBe("Linux");
		expect(systemName("")).toBe("");
	});

	it("opens an issue holding the report template, filled with what the editor knows", () => {
		const url = new URL(reportUrl({ version: "0.14.0", board: "Arduino Uno" }, "Mozilla/5.0 (X11; Linux x86_64)"));
		expect(url.origin + url.pathname).toBe("https://github.com/Zargith/Scratch2Arduino/issues/new");
		expect(url.searchParams.get("labels")).toBe("bug");
		expect(url.searchParams.get("title")).toMatch(/^\[Problème \/ Problem\] $/);
		const body = url.searchParams.get("body") ?? "";
		for (const expected of [
			"### Ce qui s'est passé / What happened",
			"### Comment le refaire / How to make it happen again",
			"### Ce que tu attendais / What you expected",
			"- [ ] Mode Live / Live mode",
			"Scratch2Arduino version: 0.14.0",
			"Operating system: Linux",
			"Board: Arduino Uno",
		]) {
			expect(body).toContain(expected);
		}
		expect(body).not.toContain("```"); // no error to show
	});

	it("puts the error of the crash screen in the report, shortened, and question marks for what is unknown", () => {
		const body = reportBody({ version: "", board: "", details: "x".repeat(4000) }, "");
		expect(body).toContain("```\nxxx");
		expect(body).toContain("[...]");
		expect(body.length).toBeLessThan(3500);
		expect(body).toContain("Scratch2Arduino version: ?");
		expect(body).toContain("Board: ?");
	});
});
