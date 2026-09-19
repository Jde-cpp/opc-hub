import {Injectable, signal, inject } from '@angular/core';
import {DocumentTitle} from '../../services/document-title';

/**
 * Service responsible for setting the title that appears above the components and guide pages.
 */
@Injectable({providedIn: 'root'})
export class ComponentPageTitle {
  private _title = signal<string>('');
  //_originalTitle = 'Angular Material UI component library';

  get title(): string {
    return this._title();
  }

  set title(title: string) {
    this._title.set(title);
    this.documentTitle.set(title);//adds the app name, and falls back to it for ''
  }
	set detail( x:string ){//
		//const main = this.title?.includes("|") ? this.title.substring( this.title.lastIndexOf('|') ) : this.title;
		this._title.set( x );
 	}
  private documentTitle:DocumentTitle = inject( DocumentTitle );
}
