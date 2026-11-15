package pes.gateway.protocol;

import com.fasterxml.jackson.annotation.JsonInclude;

@JsonInclude(JsonInclude.Include.NON_NULL)
public class RunAcceptedEvent extends ServerEvent {
    private String roomId;
    private String requestId;
    private String language;

    public RunAcceptedEvent() {
        super("run.accepted");
    }

    public RunAcceptedEvent(String roomId, String requestId, String language) {
        super("run.accepted");
        this.roomId = roomId;
        this.requestId = requestId;
        this.language = language;
    }

    public String getRoomId() {
        return roomId;
    }

    public void setRoomId(String roomId) {
        this.roomId = roomId;
    }

    public String getRequestId() {
        return requestId;
    }

    public void setRequestId(String requestId) {
        this.requestId = requestId;
    }

    public String getLanguage() {
        return language;
    }

    public void setLanguage(String language) {
        this.language = language;
    }
}
