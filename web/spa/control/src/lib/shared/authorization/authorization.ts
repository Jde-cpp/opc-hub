import { Component, Signal, computed, inject, resource, signal } from '@angular/core';
import { NgTemplateOutlet } from '@angular/common';
import {Router, RouterLink, RouterLinkActive} from '@angular/router';
import {MatButtonModule} from '@angular/material/button';
import { EProvider, IAUTH, User } from '../../services/authorization/auth';
import { IENVIRONMENT } from '../../services/environment/environment';
import { MatIconModule } from '@angular/material/icon';
import { MatTooltipModule } from '@angular/material/tooltip';

declare const gapi: any;
@Component( {
	selector: "authorization", templateUrl: "./authorization.html", styleUrls: ["./authorization.scss"],
	imports: [MatButtonModule, MatIconModule, MatTooltipModule, NgTemplateOutlet, RouterLink, RouterLinkActive]} )
export class Authorization{
	#authService = inject( IAUTH );
	constructor(){
		this.user = this.#authService.user;
	}
	async onLogout() {
		const isGoogle = this.user()?.provider == EProvider.Google;//`?.`: the signal can already be empty, and a throw here skips the logout itself
		await this.#authService.logout();
		if( isGoogle )
			await this.#signOutGoogle();
		this.router.navigate( ['/login'] );
	}

	//Best effort, and it must never strand the user on the page they just logged out of.  `gapi` is a `declare const` over
	//Google's platform.js, which a Google sign-in loads (jde-framework's GoogleAuthService), so a bare `gapi.auth2` is a
	//ReferenceError - not a falsy read - whenever that script has not loaded (offline, blocked, or a session signed in on an
	//earlier page load), and that skipped the navigate below it.  So did a signOut() that rejected.  The session is already
	//dropped locally either way; this only clears Google's own state.
	async #signOutGoogle():Promise<void>{
		try{
			const auth2 = typeof gapi!="undefined" && gapi.auth2 ? gapi.auth2.getAuthInstance() : undefined;
			if( !auth2 )
				return;
			await auth2.signOut();
			console.log( "User signed out." );
		}
		catch( e ){
			console.warn( "Google sign-out failed; the local session is already dropped.", e );
		}
	}
	providers = resource<EProvider[], {}>({
    loader: async () => {
			try{
				let providers = await this.#authService.providers();
				return providers;
			}
			catch( e ){
				console.error( e );
				return [];
			}
    }
	});

	router = inject(Router);
	user:Signal<User | undefined>;
	//A session nobody signed in to that the install's guest user stands behind (#483):  named Guest, with the sign-in link beside it as
	//an anonymous session has it.
	guest = computed<boolean>( ()=>this.#authService.guest?.() ?? false );
	//config.json's `login` false (docs/demo/spec.md *The public origin*):  a page that offers no sign-in shows no link to one.
	signIn = inject( IENVIRONMENT, {optional: true} )?.get<boolean>( 'login' )!==false;
	//Google's avatar host answers 429 to a localhost referrer under repeated loads;  one that fails shows the icon until the url changes.
	#failedPicture = signal<string|undefined>( undefined );
	picture = computed( ()=>{
		const picture = this.user()?.picture;
		return picture==this.#failedPicture() ? undefined : picture;
	});
	onPictureError(){ this.#failedPicture.set( this.user()?.picture ); }
	//the picture alone says nothing to a screen reader, nor who is signed in - the label and tooltip carry both.
	signOutLabel = computed<string>( ()=>{
		const who = this.user()?.name ?? this.user()?.email;
		return who ? `Sign out ${who}` : 'Sign out';
	});
}