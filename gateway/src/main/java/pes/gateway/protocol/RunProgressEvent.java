package pes.gateway.protocol;

import com.fasterxml.jackson.annotation.JsonInclude;

@JsonInclude(JsonInclude.Include.NON_NULL)
public class RunProgressEvent extends ServerEvent {
    private String roomId;
    private String requestId;
    private String stage;

    public RunProgressEvent() {
        super("run.progress");
    }

    public RunProgressEvent(String roomId, String requestId, String stage) {
        super("run.progress");
        this.roomId = roomId;
        this.requestId = requestId;
        this.stage = stage;
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

    public String getStage() {
        return stage;
    }

    public void setStage(String stage) {
        this.stage = stage;
    }
}
