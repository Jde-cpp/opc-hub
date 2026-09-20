import { Component, computed, effect, inject, OnDestroy, Resource, resource } from '@angular/core';
import {Router, RouterLink} from "@angular/router";
import { MatButtonModule } from '@angular/material/button'
import { MatInputModule } from '@angular/material/input';
import {MatFormFieldModule} from '@angular/material/form-field';
import { EProvider, IAUTH, IENVIRONMENT, IAuth, IEnvironment, User } from 'jde-spa';
import {FormBuilder, ReactiveFormsModule} from "@angular/forms";
import {SnackbarService} from '../../../shared/snackbar/snackbar-service';
import {GoogleAuthService, googleLoginHintKey} from '../../../services/google-auth-service';

@Component({
    selector: 'app-login-page',templateUrl: './login-page.html',styleUrl: './login-page.scss',
    imports: [MatButtonModule,MatFormFieldModule,MatInputModule,RouterLink,ReactiveFormsModule],
})
export class LoginPage implements OnDestroy{
	private snackbar:SnackbarService = inject( SnackbarService );
	private authService:IAuth = inject( IAUTH );
	private envService:IEnvironment = inject( IENVIRONMENT );
	constructor(){
		effect( async ()=>{
			if( this.providers?.value()?.includes(EProvider.Google) && !this.showedGoogleLogin ){
				this.showedGoogleLogin = true;
				if( !this.googleAuth.available )
					console.error( "google not defined" );
				else{
					let authId = await this.getGoogleAuthClientId();
					this.showGoogleLogin( authId );
				}
			}
		});

	}

  async onLogin() {
		let {username, password} = this.form.value;
		if (!username || !password) {
			this.snackbar.error( "Enter an email and password." );
			return;
		}
		let domain = undefined;
		if( username.indexOf('\\')!=-1 ){
			const parts = username.split('\\');
			domain = parts[0];
			username = parts[1];
		}
		try{
			await this.authService.loginPassword(
				username,
				password,
				domain,
				console.log
			);
			this.router.navigate( [''] );
		}
		catch( e ){
			this.snackbar.exception( "Login failed.", e );
		}
  }
	async onGoogleLogin(credential: string){
		let user = new User( credential );
		try{
			await this.authService.login( user, console.log );
			if( user.email )
				localStorage.setItem( googleLoginHintKey, user.email );
			this.router.navigate( [''] );
		}
		catch( e ){
			this.snackbar.exception( "Login failed.", e );
		};
	}

	//initialize/prompt live in GoogleAuthService so a silent prompt() can also run from any page on session expiry
	//(reviews/todo.md §7) — this page only wires its interactive handler and renders the button.
	showGoogleLogin( authId:string ){
		try{
			this.googleAuth.credentialHandler = this.#onCredential;
			this.googleAuth.initialize( authId );
			this.googleAuth.renderButton( document.getElementById("google-button") );
			this.googleAuth.prompt();//auto_select only takes effect during prompt() — silently re-signs-in a returning user after session timeout
			if( this.googleCredential && !this.user()?.picture )//user() is undefined when nobody's logged in
				this.onGoogleLogin( this.googleCredential );
		}
		catch( e ){
			console.assert( !(e instanceof ReferenceError), "ReferenceError" );
			this.snackbar.exception( "Could not render Google login.", e );
		}
	}
	ngOnDestroy():void{
		if( this.googleAuth.credentialHandler===this.#onCredential )
			this.googleAuth.credentialHandler = undefined;//don't hold the departed page; a later silent renewal settles through its own resolver
	}
	#onCredential = ( credential:string )=>{ this.onGoogleLogin( credential ); };

	async getGoogleAuthClientId():Promise<string>{
		const y = await this.authService.googleAuthClientId( console.log );
		if( !y )
			throw new Error( "googleAuthClientId is not defined" );
		return y;
	}
	//install-issues #38:  a `hasUserPassword` computed lived here - true when the providers include EProvider.OpcServer -
	//and nothing ever read it;  the form has always rendered unconditionally.  Deleted rather than wired up, deliberately:
	//the form must show on a hub that has no OPC provider yet, because adding the first server connection is done signed
	//out and this page is where the user lands afterwards.  What the form lacked was not a condition but an explanation,
	//which is now the Username hint in the template.
	providers = resource<EProvider[], {}>( {loader: async () =>await this.authService.providers( console.log )} );

	fb = inject(FormBuilder);
	googleAuth = inject(GoogleAuthService);
	private get googleCredential():string{return this.envService.get("googleCredential"); }
	private showedGoogleLogin = false;
  form = this.fb.group({ username: [''], password: [''] });
	router = inject(Router);
	user = computed<User | undefined>( () => {
		return this.authService.user();
	});
}