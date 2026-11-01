import {Component, inject, input, output } from '@angular/core';
import {ComponentPageTitle} from '../component-page-title/component-page-title';
import {MatButtonModule} from '@angular/material/button';
import {MatIconModule} from '@angular/material/icon';
import {MatTooltipModule} from '@angular/material/tooltip';
import { Router } from '@angular/router';

@Component({
	selector: 'component-page-header',
	templateUrl: './component-page-header.html',
	styleUrls: ['./component-page-header.scss'],
	imports: [MatButtonModule, MatIconModule, MatTooltipModule]
})
export class ComponentPageHeader {
	#componentPageTitle = inject( ComponentPageTitle );
	#router = inject( Router );//
	toggleSidenav = output<void>();

	getTitle() {//
		return this.#componentPageTitle.title;//
	}//
	backUrl = input<string>();
	back(){ this.#router.navigate([this.backUrl()] ); }
}