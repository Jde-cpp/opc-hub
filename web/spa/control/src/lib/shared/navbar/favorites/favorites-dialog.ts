import {Component, ElementRef, computed, inject, input, output, signal, viewChild} from '@angular/core';
import {form, FormField} from '@angular/forms/signals';
import {MatButtonModule} from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import {MatInputModule} from '@angular/material/input';
import {MatAutocompleteModule} from '@angular/material/autocomplete';
import {MatFormFieldModule} from '@angular/material/form-field';
import { MatTooltipModule } from '@angular/material/tooltip';
import { MAT_DIALOG_DATA, MatDialog, MatDialogActions, MatDialogClose, MatDialogConfig, MatDialogContent, MatDialogRef, MatDialogTitle } from '@angular/material/dialog';
import { fromEvent } from 'rxjs';
import { Favorite } from '../navbar';
import { DocumentTitle } from '../../../services/document-title';

type DialogData = { existing:Favorite, folderNames:string[], name:string };
@Component( {
	selector: "favorites",
	//a real button:  the bare <mat-icon (click)> it replaced was aria-hidden and unreachable by keyboard.
	template: `<button #icon matIconButton type="button" aria-haspopup="dialog" [attr.aria-label]="label()" [matTooltip]="label()" (click)="onClick()">
		<mat-icon [class.highlight]="isFavorite()">star</mat-icon>
	</button>`,
	styles: ".highlight { color: gold; }",
	imports: [MatButtonModule, MatIconModule, MatTooltipModule]
})
export class Favorites {
	onClick(){
		const folderNames = [ ...new Set(this.favorites().filter(fav=>fav.folderName).map( fav=>fav.folderName )) ];
		const dialogConfig: MatDialogConfig = {
			...this.#placement(),
			maxWidth: 'calc(100vw - 16px)',//Material's 80vw default would squeeze it under `width`
			data:{existing: this.existing(), name: this.name(), folderNames: folderNames}
		};
		let ref = this.dialog.open( FavoritesDialog, dialogConfig );
		const resize = fromEvent( window, 'resize' ).subscribe( ()=>{//a phone turned on its side
			const { width, position } = this.#placement();
			ref.updateSize( width ).updatePosition( position );
		});
		ref.afterClosed().subscribe( ( result:Favorite )=>{
			resize.unsubscribe();
			if( result || (result===null && this.isFavorite()) )
				this.onChange.emit( result );
		});
	}
	//5px below the star with its right edge 20px left of it, and on a phone what the viewport allows, 8px in from its edges
	#placement(){
		const rect = this.iconElementRef().nativeElement.getBoundingClientRect();
		const width = Math.min( 350, window.innerWidth-16 );
		return { width: `${width}px`, position: {top: `${rect.bottom+5}px`, left: `${Math.max( 8, rect.left-370 )}px`} };
	}
	iconElementRef = viewChild.required( 'icon', {read: ElementRef} );
	readonly dialog = inject(MatDialog);
	existing = input.required<Favorite|undefined>();
	favorites = input.required<Favorite[]>();
	name = input.required<string>();
  onChange = output<Favorite>();
	isFavorite = computed<boolean>( ()=>this.existing()!=null );
	label = computed<string>( ()=>this.isFavorite() ? 'Edit favorite' : 'Add to favorites' );
}

@Component({
 // selector: 'dialog-animations-example-dialog',
  templateUrl: 'favorites-dialog.html',
	styles: "mat-form-field { display: block; } .ok { background: var(--mat-sys-primary); } .remove { background: var(--mat-sys-error); }",
  imports: [MatButtonModule, MatDialogActions, MatDialogClose, MatDialogTitle, MatDialogContent, MatFormFieldModule, MatInputModule, MatAutocompleteModule, FormField],
})
export class FavoritesDialog {
	dialogRef = inject( MatDialogRef<FavoritesDialog> );
	data = inject<DialogData>( MAT_DIALOG_DATA );//an InjectionToken, so inject() takes it - unlike the string tokens elsewhere
	constructor(){
		const data = this.data;
		this.favoriteModel.set( {name: data.existing?.name || data.name || this.documentTitle.page(), folderName: data.existing?.folderName ?? ""} );//the page's name without the tab's app suffix; data.name is the resolved segment.
		this.folderNames = data.folderNames;
	};
	onRemove(): void {
		this.dialogRef.close( null );
	}
	onDone(): void {
		this.dialogRef.close( this.favoriteModel() );
	}
	folderNames = new Array<string>;
  favoriteModel = signal<{ name:string, folderName:string }>(null as any);
  favoriteForm = form(this.favoriteModel);
	documentTitle = inject(DocumentTitle);
}