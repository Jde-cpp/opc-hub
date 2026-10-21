import { Component, input, output } from '@angular/core';
import { MatIconButton } from '@angular/material/button';
import { MatButtonToggle, MatButtonToggleGroup } from '@angular/material/button-toggle';
import { MatIcon } from '@angular/material/icon';
import { MatProgressBar } from '@angular/material/progress-bar';
import { MatToolbar } from '@angular/material/toolbar';
import { MatTooltip } from '@angular/material/tooltip';
import { View } from '../../model/ql/view';
import { ViewSet } from '../../model/ql/view-set';

//A ViewSet's face, the same over every list:  Refresh, the view group and the view editor in the middle, the host's own
//controls projected left (`[left]`) and right (`[right]`, before the row count), and the reload bar under it.  The host
//acts on the outputs - selecting the view and re-reading.
@Component({
	selector: 'view-toolbar',
	templateUrl: './view-toolbar.html',
	styleUrl: './view-toolbar.scss',
	imports: [MatButtonToggle, MatButtonToggleGroup, MatIcon, MatIconButton, MatProgressBar, MatToolbar, MatTooltip]
})
export class ViewToolbar{
	views = input.required<ViewSet<View>>();
	refreshing = input<boolean>( false );
	count = input<number>();//the rows shown, where that is the whole count - none while a pager carries the range
	viewChange = output<number>();//not `change`:  a native change event bubbling out of a projected input would reach the same binding
	edit = output<void>();
	refresh = output<void>();
}