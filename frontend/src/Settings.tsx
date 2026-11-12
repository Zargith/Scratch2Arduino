// Settings window (gear button): language, updates. Kept by the host in settings.json.
import { Button, Checkbox, Code, Group, Modal, SegmentedControl, Stack, Text, Title } from "@mantine/core";
import { useState } from "react";

import { bridge, type Settings as SavedSettings } from "@/bridge";
import type { Language } from "@/catalog";
import type { MessageKey } from "@/i18n";
import { findUpdate, platform, type Release } from "@/updates";

interface SettingsProps {
	opened: boolean;
	language: Language;
	t(key: MessageKey, params?: Record<string, string>): string;
	/** Installed version ("" outside the application). */
	version: string;
	settings: SavedSettings;
	onLanguage(language: Language): void;
	onSettings(settings: SavedSettings): void;
	/** A newer release was found by "Check now". */
	onUpdate(release: Release): void;
	onClose(): void;
}

type Check =
	| { kind: "idle" }
	| { kind: "checking" }
	| { kind: "up_to_date" }
	| { kind: "failed" }
	| { kind: "found"; release: Release };

export function Settings({ opened, language, t, version, settings, onLanguage, onSettings, onUpdate, onClose }: SettingsProps) {
	const [check, setCheck] = useState<Check>({ kind: "idle" });
	/** Downloading the installer (Windows), or why it failed. */
	const [installing, setInstalling] = useState(false);
	const [installError, setInstallError] = useState("");
	const windows = platform(navigator.userAgent) === "windows";

	const install = async (release: Release) => {
		setInstalling(true);
		setInstallError("");
		try {
			// The program closes once the installer starts.
			if (!(await bridge.installUpdate(release.download, language))) await bridge.openUrl(release.page);
		} catch (error) {
			setInstallError(error instanceof Error ? error.message : String(error));
		} finally {
			setInstalling(false);
		}
	};

	const checkNow = async () => {
		setCheck({ kind: "checking" });
		try {
			const release = await findUpdate(version);
			if (release) {
				setCheck({ kind: "found", release });
				onUpdate(release);
			} else {
				setCheck({ kind: "up_to_date" });
			}
		} catch {
			setCheck({ kind: "failed" });
		}
	};

	return (
		<Modal opened={opened} onClose={onClose} title={t("settings.title")} centered>
			<Stack gap="lg">
				<Stack gap={6}>
					<Title order={6}>{t("language.label")}</Title>
					<SegmentedControl
						aria-label={t("language.label")}
						data={[
							{ value: "fr", label: "Français" },
							{ value: "en", label: "English" },
						]}
						value={language}
						onChange={(value) => onLanguage(value as Language)}
					/>
				</Stack>
				<Stack gap={6}>
					<Title order={6}>{t("settings.theme")}</Title>
					<SegmentedControl
						aria-label={t("settings.theme")}
						data={[
							{ value: "", label: t("settings.theme_system") },
							{ value: "light", label: t("settings.theme_light") },
							{ value: "dark", label: t("settings.theme_dark") },
						]}
						value={settings.theme}
						onChange={(value) => onSettings({ ...settings, theme: value as SavedSettings["theme"] })}
					/>
				</Stack>
				<Stack gap={6}>
					<Title order={6}>{t("settings.updates")}</Title>
					{version && <Text size="sm">{t("settings.version", { version })}</Text>}
					<Group gap="sm">
						<Button variant="light" loading={check.kind === "checking"} disabled={!version} onClick={() => void checkNow()}>
							{t("settings.check_now")}
						</Button>
						{check.kind === "up_to_date" && (
							<Text size="sm" c="green">
								{t("settings.up_to_date")}
							</Text>
						)}
						{check.kind === "failed" && (
							<Text size="sm" c="orange">
								{t("settings.check_failed")}
							</Text>
						)}
						{check.kind === "found" && (
							<Text size="sm" c="blue">
								{t("update.available", { version: check.release.version, current: version })}
							</Text>
						)}
					</Group>
					{check.kind === "found" && (
						<Stack gap={6}>
							<Group gap="sm">
								{windows && check.release.download.endsWith(".exe") && (
									<Button loading={installing} onClick={() => void install(check.release)}>
										{t("settings.install_update")}
									</Button>
								)}
								<Button variant="default" onClick={() => void bridge.openUrl(check.release.page).catch(() => undefined)}>
									{t("update.notes")}
								</Button>
							</Group>
							<Text size="xs" c="dimmed">
								{t(windows ? "settings.install_help" : "settings.install_help_source")}
							</Text>
							{!windows && <Code block>{"git pull\n./Scratch2Arduino_installer.sh"}</Code>}
							{installing && <Text size="sm">{t("settings.downloading")}</Text>}
							{installError && (
								<Text size="sm" c="red">
									{installError}
								</Text>
							)}
						</Stack>
					)}
					<Checkbox
						label={t("settings.check_at_start")}
						checked={settings.checkUpdates}
						onChange={(event) => onSettings({ ...settings, checkUpdates: event.currentTarget.checked })}
					/>
				</Stack>
			</Stack>
		</Modal>
	);
}
