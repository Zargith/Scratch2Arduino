import type { Language } from "@/catalog";

const fr = {
	"app.title": "Scratch2Arduino",
	"project.untitled": "Mon projet",
	"project.name": "Nom du projet",
	"toolbar.new": "Nouveau",
	"toolbar.open": "Ouvrir",
	"toolbar.save": "Enregistrer",
	"toolbar.code": "Voir le code",
	"toolbar.build": "Vérifier",
	"toolbar.upload": "Téléverser",
	"toolbar.live": "Live",
	"toolbar.stop_live": "Arrêter",
	"live.variables": "Variables",
	"board.label": "Carte",
	"port.label": "Port",
	"port.none": "Aucune carte branchée",
	"port.refresh": "Chercher les cartes branchées",
	"language.label": "Langue",
	"variables.create": "Créer une variable",
	"variables.prompt": "Nom de la nouvelle variable :",
	"dialog.ok": "Valider",
	"dialog.cancel": "Annuler",
	"code.title": "Code Arduino généré",
	"code.readonly": "Ce code est créé à partir de tes blocs. Modifie les blocs pour le changer.",
	"status.saved": "Projet enregistré : {path}",
	"status.opened": "Projet ouvert : {path}",
	"status.building": "Vérification du programme…",
	"status.uploading": "Envoi du programme sur la carte…",
	"status.details": "Détails techniques",
	"status.no_port": "Branche ta carte Arduino puis choisis son port avant de téléverser.",
	"status.no_host": "Cette action n'est possible que dans l'application Scratch2Arduino.",
	"status.error": "Une erreur inattendue s'est produite : {message}",
	"confirm.new": "Commencer un nouveau projet ? Les modifications non enregistrées seront perdues.",
} as const;

export type MessageKey = keyof typeof fr;

const en: Record<MessageKey, string> = {
	"app.title": "Scratch2Arduino",
	"project.untitled": "My project",
	"project.name": "Project name",
	"toolbar.new": "New",
	"toolbar.open": "Open",
	"toolbar.save": "Save",
	"toolbar.code": "Show code",
	"toolbar.build": "Check",
	"toolbar.upload": "Upload",
	"toolbar.live": "Live",
	"toolbar.stop_live": "Stop",
	"live.variables": "Variables",
	"board.label": "Board",
	"port.label": "Port",
	"port.none": "No board plugged in",
	"port.refresh": "Look for plugged-in boards",
	"language.label": "Language",
	"variables.create": "Make a variable",
	"variables.prompt": "Name of the new variable:",
	"dialog.ok": "OK",
	"dialog.cancel": "Cancel",
	"code.title": "Generated Arduino code",
	"code.readonly": "This code is made from your blocks. Change the blocks to change it.",
	"status.saved": "Project saved: {path}",
	"status.opened": "Project opened: {path}",
	"status.building": "Checking the program…",
	"status.uploading": "Sending the program to the board…",
	"status.details": "Technical details",
	"status.no_port": "Plug in your Arduino board, then pick its port before uploading.",
	"status.no_host": "This action only works in the Scratch2Arduino application.",
	"status.error": "Something unexpected went wrong: {message}",
	"confirm.new": "Start a new project? Unsaved changes will be lost.",
};

const messages: Record<Language, Record<MessageKey, string>> = { fr, en };

export function translate(language: Language, key: MessageKey, params: Record<string, string> = {}): string {
	return messages[language][key].replace(/\{(\w+)\}/g, (match, name: string) => params[name] ?? match);
}

export function detectLanguage(): Language {
	return navigator.language.toLowerCase().startsWith("en") ? "en" : "fr";
}
