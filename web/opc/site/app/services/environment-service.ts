import { Injectable } from '@angular/core';
import{ IEnvironment } from 'jde-spa'
import {environment} from '../../environments/environment'

@Injectable()
export class EnvironmentService implements IEnvironment{
	get<T>( key:string ): T{
		return (environment as Record<string,unknown>)[key] as T;
	}
}