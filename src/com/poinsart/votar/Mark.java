/*
    VotAR : Vote with Augmented reality
    Copyright (C) 2013 Stephane Poinsart <s@poinsart.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

package com.poinsart.votar;

public class Mark {
	// position of the mark
	public int x;
	public int y;
	
	// pattern rotation
	public int pr;
	
	public Mark(int x, int y, int pr) {
		this.x=x;
		this.y=y;
		this.pr=pr;
	}
}
