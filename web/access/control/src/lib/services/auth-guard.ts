import {inject, Injectable} from '@angular/core';
import {ActivatedRouteSnapshot, CanActivate, CanActivateChild, GuardResult, RouterStateSnapshot} from '@angular/router';
import { AccessService } from './access-service';


@Injectable( {providedIn: 'root'} )
export class AuthGuard implements CanActivate, CanActivateChild {

	canActivate( route: ActivatedRouteSnapshot, state: RouterStateSnapshot ):Promise<GuardResult>{
		return Promise.resolve(true);
	}

	canActivateChild( childRoute: ActivatedRouteSnapshot, state: RouterStateSnapshot ):Promise<GuardResult>{
		return Promise.resolve(true);
	}
	accessService = inject(AccessService);
}
