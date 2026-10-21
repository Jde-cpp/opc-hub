import { Component, ViewEncapsulation } from '@angular/core';
import { RouterOutlet } from '@angular/router';
import {NavBar} from 'jde-spa';

@Component( {selector: 'app-root', templateUrl: './app.html', styleUrls: ['./app.scss'], encapsulation: ViewEncapsulation.None,
	imports: [NavBar, RouterOutlet],
})
export class App{}