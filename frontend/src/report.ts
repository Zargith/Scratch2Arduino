// "Report a problem": opens a new GitHub issue holding a report template, filled with what the editor knows.
export const ISSUES_URL = "https://github.com/Zargith/Scratch2Arduino/issues/new";

export interface ReportContext {
	/** Installed version ("" outside the application). */
	version: string;
	/** Board name as shown in the board menu, or "" when unknown. */
	board: string;
	/** Error text to put in the description (crash screen), or "". */
	details?: string;
}

/** Readable operating system from the webview user agent. */
export function systemName(userAgent: string): string {
	if (/windows/i.test(userAgent)) return "Windows";
	if (/mac os x|macintosh/i.test(userAgent)) return "macOS";
	if (/linux|x11/i.test(userAgent)) return "Linux";
	return "";
}

/**
 * The text of the issue, a template the person completes: what happened, how to make it happen
 * again, what they expected, then what the editor knows (version, system, board, error). The same
 * sections as the "Report a problem" form of .github/ISSUE_TEMPLATE/bug_report.yml, in plain text:
 * GitHub reads issue forms from the default branch only, and the text works everywhere.
 */
export function reportBody(context: ReportContext, userAgent: string): string {
	// Long stack traces would make the link too long for some browsers.
	const details = context.details && context.details.length > 1500 ? `${context.details.slice(0, 1500)}\n[...]` : context.details;
	const lines = [
		"### Ce qui s'est passé / What happened",
		"",
		"<!-- Décris ce que tu as vu : message d'erreur, fenêtre, carte qui ne réagit pas... / Describe what you saw: error message, window, board that does not react... -->",
		"",
	];
	if (details) lines.push("```", details, "```", "");
	lines.push(
		"### Comment le refaire / How to make it happen again",
		"",
		"<!-- Les étapes, une par ligne. Tu peux joindre ton projet .s2a (glisse-le ici). / The steps, one per line. You can attach your .s2a project (drop it here). -->",
		"1. ",
		"2. ",
		"3. ",
		"",
		"### Ce que tu attendais / What you expected",
		"",
		"",
		"### Pendant / While",
		"",
		"- [ ] Construction des blocs / Building blocks",
		"- [ ] Vérifier / Check",
		"- [ ] Téléverser / Upload",
		"- [ ] Mode Live / Live mode",
		"- [ ] Carte virtuelle / Virtual board",
		"- [ ] Code (C++, Python, JavaScript)",
		"- [ ] Schéma de câblage / Wiring diagram",
		"- [ ] Installation ou mise à jour / Install or update",
		"",
		"### Informations",
		"",
		`- Version de Scratch2Arduino / Scratch2Arduino version: ${context.version || "?"}`,
		`- Système / Operating system: ${systemName(userAgent) || "?"}`,
		`- Carte / Board: ${context.board || "?"}`
	);
	return lines.join("\n");
}

/** Link to a new GitHub issue already holding the report template, filled with what the editor knows. */
export function reportUrl(context: ReportContext, userAgent: string): string {
	const query = new URLSearchParams({
		title: "[Problème / Problem] ",
		labels: "bug",
		body: reportBody(context, userAgent),
	});
	return `${ISSUES_URL}?${query.toString()}`;
}
