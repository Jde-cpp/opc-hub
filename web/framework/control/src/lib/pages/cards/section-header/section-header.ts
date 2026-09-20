import { Component, computed, effect, inject, input, signal } from '@angular/core';
import { Router } from '@angular/router';
import { MatIconModule } from '@angular/material/icon';
import { CARD_STATUS, CardStatusValue, statusFor } from '../card-status';

//A section page's header:  the section it sits under (its top-level route's icon and title), the page's own heading, and
//the page's live summary - the line its home tile or its parent's card shows, from the same CARD_STATUS provider, so the
//two always agree.  The host page supplies the tint (`.section-page.card-<section>`).
@Component( {
	selector: 'section-header',
	templateUrl: './section-header.html',
	styleUrls: ['./section-header.scss'],
	imports: [MatIconModule]
})
export class SectionHeader{
	url = input.required<string>();//the page's own, '/gateways/gw1'
	heading = input<string>( '' );
	constructor(){
		effect( ()=>{
			const url = this.url(), generation = ++this.#generation;
			this.status.set( undefined );
			statusFor( this.#providers, url )?.status( url ).then(
				value=>{ if( generation==this.#generation ) this.status.set( value ); },
				()=>{} );//no rights, a service down:  the header goes without its line
		});
	}
	#section = computed( ()=>{
		const first = this.url().split( '/' ).find( s=>s.length );
		return this.#router.config.find( r=>r.path==first && r.title );
	});
	name = computed( ()=>this.#section()?.title as string|undefined );
	icon = computed( ()=>this.#section()?.data?.['icon'] as string|undefined );
	status = signal<CardStatusValue|undefined>( undefined );
	#generation = 0;
	#providers = inject( CARD_STATUS, {optional: true} ) ?? [];
	#router = inject( Router );
}
