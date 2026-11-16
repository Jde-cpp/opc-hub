import { Component, inject, ViewEncapsulation } from '@angular/core';
import { RouterOutlet } from '@angular/router';
import { IENVIRONMENT, NavBar } from 'jde-spa';

@Component( {selector: 'app-root', templateUrl: './app.html', styleUrls: ['./app.scss'], encapsulation: ViewEncapsulation.None,
	imports: [NavBar, RouterOutlet],
})
export class App{
	banner = inject( IENVIRONMENT ).get<string>( 'banner' );//config.json's line above every page (EnvironmentService)
}