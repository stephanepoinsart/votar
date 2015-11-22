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

/*
 * this file handle a small webserver for the remote result display (laptop + video-projector)
 */

package com.poinsart.votar;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.TimeUnit;


import android.util.Log;
import fi.iki.elonen.NanoHTTPD;

public class VotarWebServer extends NanoHTTPD {
	private VotarMain mainact;
	
	public static final int SOCKET_READ_TIMEOUT = 65000;

	public VotarWebServer(int port, VotarMain mainact) {
		super(port);
		this.mainact=mainact;
		mainact.updateWifiStatus();
	}
	

    private Response createResponse(Response.Status status, String mimeType, String message) {
        Response res = new Response(status, mimeType, message);
        res.addHeader("Access-Control-Allow-Origin", "*");
        if (status!=Response.Status.OK)
        	Log.w("Votar WebServer", message);
        return res;
    }
    
    private Response createResponse(Response.Status status, String mimeType, InputStream message) {
        Response res = new Response(status, mimeType, message);
        res.addHeader("Access-Control-Allow-Origin", "*");
        return res;
    }
	
	public Response serve(IHTTPSession session) {
		String uri=session.getUri();
	
		
		//Log.i("Votar WebServer", "Request: "+uri);
		
		if (uri.equals("/photo")) {
			if (mainact.photoLock==null) {
				return createResponse(Response.Status.NOT_FOUND, NanoHTTPD.MIME_PLAINTEXT, "Error 404 (NOT FOUND). The file is not ready because no photo has been used yet.");
			}
			try {
				if (!mainact.photoLock.await(60, TimeUnit.SECONDS)) {
					return createResponse(Response.Status.INTERNAL_ERROR, NanoHTTPD.MIME_PLAINTEXT, "Error 500 (INTERNAL SERVER ERROR). The process was locked for too long and can't deliver the file.");
				}
			} catch (InterruptedException e) {
				return createResponse(Response.Status.INTERNAL_ERROR, NanoHTTPD.MIME_PLAINTEXT, "Error 500 (INTERNAL SERVER ERROR). The process was interrupted before the server could deliver the file.");
			}
			if (mainact.lastPhotoFilePath==null) {
				return createResponse(Response.Status.NOT_FOUND, NanoHTTPD.MIME_PLAINTEXT, "Error 404 (NOT FOUND). The file is not ready because no photo has been used yet.");
			}
			FileInputStream fis;
			try {
				fis = new FileInputStream(new File(mainact.lastPhotoFilePath));
			} catch (FileNotFoundException e) {
				return createResponse(Response.Status.INTERNAL_ERROR, NanoHTTPD.MIME_PLAINTEXT, "Error 500 (INTERNAL SERVER ERROR). The server failed while attempting to read the file to deliver.");
			}
			return createResponse(Response.Status.OK, "image/jpeg", fis);
		}
		if (uri.equals("/points")) {
			if (mainact.photoLock==null) {
				return createResponse(Response.Status.NOT_FOUND, NanoHTTPD.MIME_PLAINTEXT, "Error 404 (NOT FOUND). The file is not ready because no photo has been used yet.");
			}
			try {
				if (!mainact.pointsLock.await(60, TimeUnit.SECONDS)) {
					return createResponse(Response.Status.INTERNAL_ERROR, NanoHTTPD.MIME_PLAINTEXT, "Error 500 (INTERNAL SERVER ERROR). The process was locked for too long and can't deliver the file.");
				}
			} catch (InterruptedException e) {
				return createResponse(Response.Status.INTERNAL_ERROR, NanoHTTPD.MIME_PLAINTEXT, "Error 500 (INTERNAL SERVER ERROR). The process was interrupted before the server could deliver the file.");
			}
			if (mainact.lastPointsJsonString==null) {
				return createResponse(Response.Status.INTERNAL_ERROR, NanoHTTPD.MIME_PLAINTEXT, "Error 500 (INTERNAL SERVER ERROR). There was an error in the application and the points data failed to be generated.");
			}
			return createResponse(Response.Status.OK, NanoHTTPD.MIME_PLAINTEXT, mainact.lastPointsJsonString);
		}
		if (uri.equals("/datatimestamp")) {
			return createResponse(Response.Status.OK, NanoHTTPD.MIME_PLAINTEXT, ""+mainact.datatimestamp);
		}
		if (uri.equals("/") || uri.equals("/footer_deco.png") || uri.equals("/votar_logo.png")) {
			String mime, filename;
			if (uri.equals("/")) {
				mime=NanoHTTPD.MIME_HTML;
				filename="index.html";
			} else {
				mime="image/png";
				filename=uri.substring(1);
			}
			InputStream is;
			try {
				is = mainact.assetMgr.open(filename);
			} catch (IOException e) {
				return createResponse(Response.Status.INTERNAL_ERROR, NanoHTTPD.MIME_PLAINTEXT, "Error 500 (INTERNAL SERVER ERROR). The server failed while attempting to read the static file to deliver.");
			}
			return createResponse(Response.Status.OK, mime, is);
			
		}
		return createResponse(Response.Status.NOT_FOUND, NanoHTTPD.MIME_PLAINTEXT, "Error 404 (NOT FOUND). I'm sorry. My responses are limited. You must ask the right questions.");
	}
}
