import { Component, input } from '@angular/core';
import { MatButton } from '@angular/material/button';
import { MatIcon } from '@angular/material/icon';
import type { DetailPage } from '../detail-page';

//The Save/Cancel/Delete row under a record's tabs, driving the DetailPage it sits on - the page passes itself.  What a page
//adds between Cancel and the trash can (client-detail's Test Connection) is projected.
@Component({
	selector: 'detail-actions',
	templateUrl: './detail-actions.html',
	styleUrl: './detail-actions.scss',
	imports: [MatButton, MatIcon]
})
export class DetailActions{
	page = input.required<DetailPage<any>>();//any:  the row reads nothing of the record
	canPurge = input<boolean>( true );//false where the table has no purge mutation (users)
}