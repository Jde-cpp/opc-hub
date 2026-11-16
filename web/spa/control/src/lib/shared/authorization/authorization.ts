import { Component, Signal, computed, inject, resource, signal } from '@angular/core';
import { NgTemplateOutlet } from '@angular/common';
import {Router, RouterLink, RouterLinkActive} from '@angular/router';
import {MatButtonModule} from '@angular/material/button';
import { EProvider, IAUTH, User } from '../../services/authorization/auth';
import { IENVIRONMENT } from '../../services/environment/environment';
import { MatIconModule } from '@angular/material/icon';
import { MatTooltipModule } from '@angular/material/tooltip';

@Component( {
	selector: "authorization", templateUrl: "./authorization.html", styleUrls: ["./authorization.scss"],
	imports: [MatButtonModule, MatIconModule, MatTooltipModule, NgTemplateOutlet, RouterLink, RouterLinkActive]} )
export class Authorization{
	#authService = inject( IAUTH );
	constructor(){
		this.user = this.#authService.user;
	}
	async onLogout() {//a Google user's sign-out reaches Google in the logout itself (jde-framework's GoogleAuthService.signOut)
		await this.#authService.logout();
		this.router.navigate( ['/login'] );
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