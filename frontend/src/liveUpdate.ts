import type { MessageKey } from "@/i18n";

/**
 * Hint shown in Live mode when the edited blocks cannot run yet (half-built): the board keeps the
 * previous ones. A hint, not an error: the child is still editing (audit #179). Null when sent.
 */
export function notAppliedHint(
	result: { ok: boolean; message?: string } | undefined,
	t: (key: MessageKey, params?: Record<string, string>) => string
): string | null {
	if (!result || result.ok) return null;
	return t("live.not_applied", { message: result.message ?? "" });
}
