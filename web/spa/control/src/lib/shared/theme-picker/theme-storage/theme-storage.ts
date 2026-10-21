import {Injectable} from '@angular/core';

export interface DocsSiteTheme {
	name: string;
	displayName?: string;
	color: string;
	background: string;
	isDefault?: boolean;
}


@Injectable({providedIn: 'root'})
export class ThemeStorage {
	static storageKey = 'docs-theme-storage-current-name';

	storeTheme(theme: DocsSiteTheme) {
		try {
			window.localStorage[ThemeStorage.storageKey] = theme.name;
		} catch { }
	}

	getStoredThemeName(): string | null {
		try {
			return window.localStorage[ThemeStorage.storageKey] || null;
		} catch {
			return null;
		}
	}
}